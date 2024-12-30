#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <cstring>
#include <cstdlib>
#include <zlib.h>
#include <cstdint>

struct ChunkData {
    size_t original_size;
    size_t compressed_size;
    uint32_t crc;
    std::vector<uint8_t> compressed_data;
};

struct DecompressedData {
    std::vector<uint8_t> data;
};

uint32_t compute_crc(const uint8_t* data, size_t size) {
    return crc32(0, data, size);
}

void compress_with_zlib(const uint8_t* data, size_t data_size, std::vector<uint8_t>& output) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    deflateInit(&stream, Z_DEFAULT_COMPRESSION);

    stream.avail_in = data_size;
    stream.next_in = const_cast<Bytef*>(data);

    output.resize(data_size * 1.1 + 12);
    stream.avail_out = output.size();
    stream.next_out = output.data();
    deflate(&stream, Z_FINISH);

    if (stream.avail_out == 0 || stream.avail_in != 0) {
        output.resize(output.size() * 2);
        stream.avail_out = output.size() - stream.total_out;
        stream.next_out = output.data() + stream.total_out;
        deflate(&stream, Z_FINISH);
    }

    output.resize(stream.total_out);
    deflateEnd(&stream);
}

void decompress_with_zlib(const uint8_t* compressed_data, size_t compressed_size, std::vector<uint8_t>& output) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    inflateInit(&stream);

    stream.avail_in = compressed_size;
    stream.next_in = const_cast<Bytef*>(compressed_data);

    output.resize(compressed_size * 10);
    stream.avail_out = output.size();
    stream.next_out = output.data();
    inflate(&stream, Z_SYNC_FLUSH);

    output.resize(stream.total_out);
    inflateEnd(&stream);
}

ChunkData compress_chunk(size_t index, const uint8_t* data, size_t data_size) {
    uint32_t crc = compute_crc(data, data_size);
    std::vector<uint8_t> compressed_data;
    compress_with_zlib(data, data_size, compressed_data);
    return {data_size, compressed_data.size(), crc, compressed_data};
}

DecompressedData decompress_chunk(size_t index, const uint8_t* compressed_data, size_t compressed_size, uint32_t crc) {
    std::vector<uint8_t> decompressed_data;
    decompress_with_zlib(compressed_data, compressed_size, decompressed_data);
    uint32_t computed_crc = compute_crc(decompressed_data.data(), decompressed_data.size());
    if (computed_crc != crc) {
        throw std::runtime_error("CRC mismatch for chunk " + std::to_string(index));
    }
    return {decompressed_data};
}

void compress_file(const std::string& input_filename) {
    const std::string output_filename = input_filename + ".compressed";

    std::ifstream infile(input_filename, std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        throw std::runtime_error("Failed to open input file.");
    }

    size_t file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    size_t chunk_size = 1 * 1024 * 1024;
    size_t total_chunks = file_size / chunk_size;
    if (file_size % chunk_size != 0) {
        total_chunks++;
    }

    std::ofstream outfile(output_filename, std::ios::binary);
    if (!outfile.is_open()) {
        throw std::runtime_error("Failed to open output file.");
    }

    outfile.write("MYCOMP", 6);
    outfile.write(reinterpret_cast<const char*>(&total_chunks), sizeof(total_chunks));

    std::vector<std::future<ChunkData>> futures(total_chunks);

    for (size_t i = 0; i < total_chunks; i++) {
        size_t read_size = chunk_size;
        if (i == total_chunks - 1) {
            read_size = file_size - i * chunk_size;
        }

        std::vector<uint8_t> chunk_data(read_size);
        infile.read(reinterpret_cast<char*>(chunk_data.data()), read_size);
        if (infile.gcount() != static_cast<std::streamsize>(read_size)) {
            throw std::runtime_error("Failed to read input file.");
        }

        futures[i] = std::async(std::launch::async, compress_chunk, i, chunk_data.data(), read_size);
    }

    for (size_t i = 0; i < total_chunks; i++) {
        auto chunk = futures[i].get();
        outfile.write(reinterpret_cast<const char*>(&chunk.original_size), sizeof(chunk.original_size));
        outfile.write(reinterpret_cast<const char*>(&chunk.compressed_size), sizeof(chunk.compressed_size));
        outfile.write(reinterpret_cast<const char*>(&chunk.crc), sizeof(chunk.crc));
        outfile.write(reinterpret_cast<const char*>(chunk.compressed_data.data()), chunk.compressed_data.size());
    }

    infile.close();
    outfile.close();
}

void decompress_file(const std::string& input_filename) {
    size_t dot_pos = input_filename.find_last_of('.');
    std::string output_filename = input_filename.substr(0, dot_pos);

    std::ifstream infile(input_filename, std::ios::binary);
    if (!infile.is_open()) {
        throw std::runtime_error("Failed to open input file.");
    }

    char magic[6];
    infile.read(magic, 6);
    if (strncmp(magic, "MYCOMP", 6) != 0) {
        throw std::runtime_error("Invalid file format.");
    }

    size_t total_chunks;
    infile.read(reinterpret_cast<char*>(&total_chunks), sizeof(total_chunks));

    std::ofstream outfile(output_filename, std::ios::binary);
    if (!outfile.is_open()) {
        throw std::runtime_error("Failed to open output file.");
    }

    std::vector<std::future<DecompressedData>> futures(total_chunks);

    for (size_t i = 0; i < total_chunks; i++) {
        size_t original_size, compressed_size;
        infile.read(reinterpret_cast<char*>(&original_size), sizeof(original_size));
        infile.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
        uint32_t crc;
        infile.read(reinterpret_cast<char*>(&crc), sizeof(crc));

        std::vector<uint8_t> compressed_data(compressed_size);
        infile.read(reinterpret_cast<char*>(compressed_data.data()), compressed_size);
        if (infile.gcount() != static_cast<std::streamsize>(compressed_size)) {
            throw std::runtime_error("Failed to read compressed data.");
        }

        futures[i] = std::async(std::launch::async, decompress_chunk, i, compressed_data.data(), compressed_size, crc);
    }

    for (size_t i = 0; i < total_chunks; i++) {
        auto decompressed = futures[i].get();
        outfile.write(reinterpret_cast<const char*>(decompressed.data.data()), decompressed.data.size());
    }

    infile.close();
    outfile.close();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <compress|decompress> <filename>" << std::endl;
        return 1;
    }

    std::string command = argv[1];
    std::string filename = argv[2];

    try {
        if (command == "compress") {
            compress_file(filename);
            std::cout << "Compression complete." << std::endl;
        } else if (command == "decompress") {
            decompress_file(filename);
            std::cout << "Decompression complete." << std::endl;
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}


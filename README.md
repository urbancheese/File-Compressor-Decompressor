# Multi-Threaded File Compression Tool

## Project Overview

This is a C++ impleementation of a multi-threaded file compression and decompression tool. It utilizes the zlib library for compression and decompression, processingg files in chunks to handle large files efficieently.

## Features

- **Compression Algorithm**: Uses zlib for efficient comvpression and decompression.
- **Multithreading**: Processes file chunks in paarallel for improved performance.
- **File Handling**: Supports files up to 10GB efficiently.
- **CLI Commands**:
  - `compress <filename>`: Compressses the specified file.
  - `decompress <filename>`: Decompresses the specified file.
- **Error Handling**: Includes checks for file existence, I/O errors, and data integrity using CRC32..
- **Performance Optimization**: Processes files in chunks and managess memory usage effectively.

#pragma once

#include <iostream>
#include <fstream>
#include <vector>

namespace systk
{

static std::vector<uint8_t> LoadFile(char const* _path)
{
    std::ifstream source_file(_path, std::ios_base::binary);
    if (!source_file.is_open())
    {
        std::cout << "Error loading file " << _path << std::endl;
        return {};
    }

    source_file.seekg(0, std::ios_base::end);
    size_t size = source_file.tellg();
    source_file.seekg(0, std::ios_base::beg);

    std::vector<uint8_t> memory{};
    memory.resize(size);

    source_file.read((char*)memory.data(), size);

    return memory;
}

} // namespace systk

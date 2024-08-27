#pragma once

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace systk
{
using StdClock = std::chrono::high_resolution_clock;

struct Timer
{
    using OnExitCallback = std::function<void(std::string const&, float)>;
    Timer(std::string _tag, OnExitCallback _on_exit)
        : tag{ _tag }
        , on_exit{ _on_exit }
        , begin{ StdClock::now() }
    {}

    ~Timer()
    {
        using namespace std::chrono;
        float const measured_time = static_cast<float>(
            duration_cast<milliseconds>(StdClock::now() - begin).count()
        ) / 1000.f;
        on_exit(tag, measured_time);
    }

    std::string tag;
    OnExitCallback on_exit;
    StdClock::time_point begin;
};

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

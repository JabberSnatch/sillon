#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

#include "systk.hh"
#include "numtk.hh"

namespace {

void ConstantResources(void*, vktk::Context*)
{
    std::vector<uint8_t> testfile = systk::LoadFile("../r.-1.6.mca");
    mctk::MCAFileData mca_data = mctk::MCADecode(testfile.data(), testfile.size());

    std::vector<mctk::NBTField> chunk_fields = mctk::MCAPackedNBTDecode(mca_data.chunks[700]);
    if (chunk_fields.empty())
        return;

    using namespace mctk;

    // -6
    // 213

    // -1
    // 6

    // -32 -> -1
    // 192 -> 223

    // 26
    // 21
    // -> 698

    NBTCompound const& root = *(NBTCompound const*)chunk_fields[0].payload;
    for (NBTField const& field0 : root)
    {
        if (field0.name == "Status"
            && field0.type == TAG_String
            && *(NBTString const*)field0.payload != "full")
            std::cout << "incomplete chunk, Status is "
                      << *(NBTString const*)field0.payload << std::endl;

        if (field0.name == "sections"
            && field0.type == TAG_List)
        {
            NBTList const& sections = *(NBTList const*)field0.payload;
            NBTCompound const* compounds = (NBTCompound const*)sections.data;
            for (uint32_t section_index = 0u; section_index < sections.size; ++section_index)
            {
                NBTCompound const& compound = compounds[section_index];
                for (NBTField const& field1 : compound)
                {
                    if (field1.name == "Y"
                        && field1.type == TAG_Byte)
                        std::cout << "Y " << (int32_t)*(int8_t const*)field1.payload << std::endl;

                    if (field1.name == "block_states"
                        && field1.type == TAG_Compound)
                    {
                        NBTCompound const& block_states = *(NBTCompound const*)field1.payload;
                        NBTLongArray const* block_data_field = nullptr;
                        uint32_t palette_size = 0u;
                        std::vector<std::string> palette_data = {};
                        for (NBTField const& field2 : block_states)
                        {
                            if (field2.name == "palette"
                                && field2.type == TAG_List)
                            {
                                NBTList const& palette = *((NBTList const*)field2.payload);
                                NBTCompound const* blocks = (NBTCompound const*)palette.data;
                                palette_size = palette.size;
                                for (uint32_t block_index = 0u;
                                     block_index < palette_size;
                                     ++block_index)
                                {
                                    NBTCompound const& block = blocks[block_index];
                                    for (NBTField const& field3 : block)
                                    {
                                        if (field3.name == "Name"
                                            && field3.type == TAG_String)
                                        {
#if 0
                                            std::cout << "\t"
                                                      << *(NBTString const*)field3.payload
                                                      << std::endl;
#endif
                                            palette_data.push_back(
                                                (*(NBTString const*)field3.payload).substr(10));
                                        }
                                    }
                                }
                            }

                            if (field2.name == "data"
                                && field2.type == TAG_Long_Array)
                                block_data_field = (NBTLongArray const*)field2.payload;
                        }

                        if (palette_size == 1)
                        {
                            std::cout << "solid chunk" << std::endl;
                            continue;
                        }

                        NBTLongArray const& block_data = *block_data_field;
                        uint32_t bit_count = numtk::log2(palette_size);
                        if (palette_size - (1 << bit_count))
                            ++bit_count;
                        bit_count = std::max(bit_count, 4u);
#if 0
                        std::cout << bit_count << std::endl;
                        uint32_t long_count = 4096 / (64/bit_count);
                        std::cout << long_count << " " << block_data.size() << std::endl;
                        for (uint32_t index = 0u; index < block_data.size(); ++index)
                            std::cout << std::hex << block_data[index] << " ";
                        std::cout << std::dec << std::endl;
#endif

                        std::array<uint16_t, 4096> blocks = {};
                        uint64_t block_index = 0;
                        uint32_t data_index = 0;
                        uint8_t data_offset = 0;
                        while (block_index < 4096 && data_index < block_data.size())
                        {
                            blocks[block_index] = (uint16_t)
                                ((block_data[data_index] >> data_offset)
                                 & ((1ull << bit_count) - 1));
                            data_offset += bit_count;
                            if (data_offset > 64)
                            {
                                data_offset = 0;
                                ++data_index;
                            }
                            ++block_index;
                        }

#if 0
                        for (uint16_t block : blocks)
                            std::cout << palette_data[block] << " ";
                        std::cout << std::endl;
#endif
                    }
                }
            }
        }
    }
    return;
}

bool Update(void*, vktk::Context*, iotk::input_t const*, iotk::input_t const*)
{
    return false;
}

}

namespace Voxels
{

Interface::Interface()
    : ConstantResources{ ::ConstantResources }
    , RenderResources{ nullptr }
    , Update{ ::Update }
    , Draw{ nullptr }
    , Shutdown{ nullptr }
    , state{ nullptr }
{}

Interface::~Interface()
{}

}

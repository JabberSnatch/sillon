#pragma once

#include <vector>
#include <numtk.hh>
#include <dstk.hh>

namespace guitk
{

struct Context_t;
using Handle_t = uint32_t;

static constexpr Handle_t kNullHandle = 0u;

using Context = Context_t*;
using Frame = Handle_t;

struct GlyphInfo
{
    numtk::bounds2u bounds;
    uint32_t index;
};

struct WidgetStates
{
    Frame current_frame;
    std::vector<numtk::bounds2u> frames;
    std::vector<GlyphInfo> glyphs;
    bool mouse_over;
};

Context CreateContext();
void DeleteContext(Context);

void BeginRecord(Context, iotk::input_t const*, iotk::input_t const*);
WidgetStates const* EndRecord(Context);

void BeginFrame(Context, Frame&);
void EndFrame(Context);

void Label(Context, std::string const& label_);

uint64_t const* FontData();

} // namespace guitk


namespace guitk {

struct FrameInfo
{
    static inline numtk::vec2u const kEmptySize{ 50, 50 };
    static inline numtk::vec2u const kOffsetIncrement{ 20, 20 };
    numtk::bounds2u bounds;
    std::vector<GlyphInfo> glyphs;
};

struct Context_t
{
    bool record_open;
    WidgetStates current_record;
    WidgetStates last_record;

    iotk::input_t const* input = nullptr;
    iotk::input_t const* past_input = nullptr;

    dstk::OrderedVector<Frame, FrameInfo> registered_frames = {};
    std::vector<Frame> frame_order = {};
    Frame grabbed_frame = {};

    Handle_t next_id = 1u;
};

Context CreateContext() { return new Context_t{}; }
void DeleteContext(Context context_) { delete context_; }

void BeginRecord(Context context_, iotk::input_t const* input_, iotk::input_t const* past_input_)
{
    context_->record_open = true;
    context_->current_record = WidgetStates{};
    context_->input = input_;
    context_->past_input = past_input_;
}

WidgetStates const* EndRecord(Context context_)
{
    WidgetStates& states = context_->current_record;
    states.frames.reserve(context_->frame_order.size());
    for (uint32_t index = 0; index < (uint32_t)context_->frame_order.size(); ++index)
    {
        Frame frame = context_->frame_order[index];
        FrameInfo const& frame_info = context_->registered_frames[frame];
        states.frames.push_back(frame_info.bounds);

        for (GlyphInfo const& glyph : frame_info.glyphs)
            states.glyphs.push_back(glyph);
    }

    context_->record_open = false;
    context_->last_record = std::move(context_->current_record);
    return &context_->last_record;
}

void BeginFrame(Context context_, Frame& frame_) {
    WidgetStates& states = context_->current_record;

    if (states.current_frame)
        return;

    if (!context_->registered_frames.contains(frame_))
    {
        frame_ = context_->next_id++;
        context_->registered_frames.insert(frame_,
                                           FrameInfo{
                                               FrameInfo::kOffsetIncrement * (context_->frame_order.size() + 1),
                                               FrameInfo::kEmptySize
                                           });
        context_->frame_order.push_back(frame_);
    }

    FrameInfo& frame_info = context_->registered_frames[frame_];
    if (frame_info.bounds.Contains(*(numtk::vec2u*)context_->input->cursor))
    {
        states.mouse_over = true;

        if (iotk::MousePress(iotk::kLeftBtn, *context_->input, *context_->past_input))
        {
            auto frame_it = std::find(context_->frame_order.begin(), context_->frame_order.end(), frame_);
            if (frame_it != context_->frame_order.end() &&
                std::none_of(std::next(frame_it), context_->frame_order.end(), [context_](Frame frame){
                    return context_->registered_frames[frame].bounds.Contains(*(numtk::vec2u*)context_->input->cursor);
                }))
            {
                context_->frame_order.erase(frame_it);
                context_->frame_order.push_back(frame_);
                context_->grabbed_frame = frame_;
            }
        }
    }

    if (iotk::MouseHold(iotk::kLeftBtn, *context_->input, *context_->past_input)
        && frame_ == context_->grabbed_frame)
    {
        frame_info.bounds.min += iotk::MouseDelta(*context_->input, *context_->past_input).cast<uint32_t>();
        states.mouse_over = true;
    }

    if (iotk::MouseRelease(iotk::kLeftBtn, *context_->input, *context_->past_input))
        context_->grabbed_frame = 0;

    frame_info.glyphs.clear();

    states.current_frame = frame_;
}

void EndFrame(Context context_) {
    WidgetStates& states = context_->current_record;

    if (states.current_frame == kNullHandle)
        return;

    states.current_frame = kNullHandle;
}

static inline uint32_t GetGlyphIndex(char character_)
{
    return (character_ >= '!') ? (character_ - '!' + 1) : 0;
}

void Label(Context context_, std::string const& label_)
{
    WidgetStates& states = context_->current_record;

    if (states.current_frame == kNullHandle)
        return;

    numtk::vec2u glyph_max = numtk::vec2u{ 0, 0 };
    FrameInfo& frame_info = context_->registered_frames[states.current_frame];
    for (uint32_t char_index = 0u; char_index < (uint32_t)label_.size(); ++char_index)
    {
        char character = label_[char_index];
        if (character == ' ')
            continue;
        uint32_t glyph_index = GetGlyphIndex(character);
        numtk::bounds2u glyph_bounds{
            frame_info.bounds.min + numtk::vec2u{ 15, 15 } + numtk::vec2u{ char_index * 14, 0 },
            { 12, 20 }
        };

        frame_info.glyphs.push_back(GlyphInfo{ glyph_bounds, glyph_index });

        glyph_max = numtk::max(glyph_max, glyph_bounds.max());
    }

    frame_info.bounds = frame_info.bounds.Expand(glyph_max + numtk::vec2u{ 15, 15 });
}

uint64_t const* FontData()
{
    static constexpr uint64_t kFontData[95] = {
        0x0fe186186186187full, 0x0000104104100100ull, 0x0000514000000000ull, 0x0000114f94f94400ull,
        0x000872a81c0aa708ull, 0x00008b4108416880ull, 0x00002145199a2740ull, 0x0000410000000000ull,
        0x0004208410408204ull, 0x0008104082084108ull, 0x0000008a9ca88000ull, 0x000000823e208000ull,
        0x0000000000010400ull, 0x000000003c000000ull, 0x0000000000000100ull, 0x0000084108410800ull,
        0x00007228aa8a2700ull, 0x0000218a08208f80ull, 0x000072208c420f80ull, 0x000072208c0a2700ull,
        0x0000410924f84100ull, 0x0000f2081c0a2700ull, 0x000031083c8a2700ull, 0x0000fa2084208200ull,
        0x000072289c8a2700ull, 0x000072289e084600ull, 0x0000000100004000ull, 0x0000000400010400ull,
        0x0000000108408100ull, 0x000000001c01c000ull, 0x0000000408108400ull, 0x0000722084200200ull,
        0x000001c8aaba0700ull, 0x00007228be8a2880ull, 0x0000e2493e8a2f00ull, 0x0000722820822700ull,
        0x0000f228a28a2f00ull, 0x0000f20838820f00ull, 0x0000f20838820800ull, 0x00007228268a2700ull,
        0x00008a28be8a2880ull, 0x0000708208208700ull, 0x0000784104124600ull, 0x0000924a38924900ull,
        0x0000820820820f00ull, 0x00008b6aa28a2880ull, 0x00008b2caa9a6880ull, 0x00007228a28a2700ull,
        0x0000f228bc820800ull, 0x00007228a28a4680ull, 0x0000f228bc922880ull, 0x000072281c0a2700ull,
        0x0000f88208208200ull, 0x00008a28a28a2700ull, 0x00008a28a2514200ull, 0x00008a28a2ab6880ull,
        0x00008a2508522880ull, 0x00008a2508208200ull, 0x0000f82108420f80ull, 0x001c410410410700ull,
        0x0000810408104080ull, 0x001c104104104700ull, 0x0000214880000000ull, 0x0000000000000fc0ull,
        0x0000408000000000ull, 0x00000007027a2780ull, 0x0000820b328a2f00ull, 0x0000000722822700ull,
        0x00000826a68a2780ull, 0x0000000722fa0700ull, 0x0000310438410400ull, 0x00000007a289e09cull,
        0x0000820b328a2880ull, 0x0000000400410400ull, 0x0000000100104918ull, 0x0000820824a38900ull,
        0x0000410410410200ull, 0x0000000d2aaa2880ull, 0x0000000b328a2880ull, 0x00000007228a2700ull,
        0x0000000b328bc820ull, 0x00000006a689e082ull, 0x0000000b32820800ull, 0x0000000720702700ull,
        0x0000410e10410200ull, 0x00000008a28a6680ull, 0x00000008a2894200ull, 0x00000008a2aaa500ull,
        0x0000000894214880ull, 0x000000092491c138ull, 0x0000000f04620f00ull, 0x000c41042041040cull,
        0x0000208208208200ull, 0x000c08208108208cull, 0x0000000019980000ull
    };

    return kFontData;
}

} // namespace guitk

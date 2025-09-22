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

struct FrameInfo
{
    static inline numtk::vec2u const kEmptySize{ 200, 50 };
    static inline numtk::vec2u const kOffsetIncrement{ 20, 20 };
    numtk::bounds2u bounds;
};

struct WidgetStates
{
    numtk::vec2u canvas_size;
    Frame current_frame;
    std::vector<FrameInfo> frames;
};

Context CreateContext();
void DeleteContext(Context);

void BeginRecord(Context, iotk::input_t const*, iotk::input_t const*);
WidgetStates const* EndRecord(Context);

void SetCanvasSize(Context, numtk::vec2u const& size);

void BeginFrame(Context, Frame&);
void EndFrame(Context);

} // namespace guitk


namespace guitk {

struct Context_t
{
    bool record_open;
    WidgetStates current_record;
    WidgetStates last_record;

    iotk::input_t const* input = nullptr;
    iotk::input_t const* past_input = nullptr;

    dstk::OrderedVector<Frame, FrameInfo> registered_frames = {};

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
    context_->record_open = false;
    context_->last_record = std::move(context_->current_record);
    return &context_->last_record;
}

void SetCanvasSize(Context context_, numtk::vec2u const& size_)
{
    if (!context_->record_open)
        return;
    context_->current_record.canvas_size = size_;
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
                                               FrameInfo::kOffsetIncrement * (states.frames.size() + 1),
                                               FrameInfo::kEmptySize
                                           });
    }

    states.frames.push_back(context_->registered_frames[frame_]);
    states.current_frame = frame_;
}

void EndFrame(Context context_) {
    WidgetStates& states = context_->current_record;

    if (states.current_frame == kNullHandle)
        return;

    states.current_frame = kNullHandle;
}

} // namespace guitk

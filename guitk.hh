#pragma once

#include <vector>
#include <numtk.hh>

namespace guitk
{

struct Context_t;

using Context = Context_t*;

struct Frame
{
    static inline numtk::vec2u const kEmptySize{ 200, 50 };
    static inline numtk::vec2u const kOffsetIncrement{ 20, 20 };
    numtk::bounds2u bounds;
};

struct WidgetStates
{
    numtk::vec2u canvas_size;
    Frame* current_frame;
    std::vector<Frame> frames;
};

Context CreateContext();
void DeleteContext(Context);

void BeginRecord(Context);
WidgetStates const* EndRecord(Context);

void SetCanvasSize(Context, numtk::vec2u const& size);

void BeginFrame(Context);
void EndFrame(Context);

} // namespace guitk


namespace guitk {

struct Context_t
{
    bool record_open;
    WidgetStates current_record;
    WidgetStates last_record;
};

Context CreateContext() { return new Context_t{}; }
void DeleteContext(Context context_) { delete context_; }

void BeginRecord(Context context_)
{
    context_->record_open = true;
    context_->current_record = WidgetStates{};
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

void BeginFrame(Context context_) {
    WidgetStates& states = context_->current_record;

    if (states.current_frame)
        return;

    states.frames.emplace_back(Frame{
        Frame::kOffsetIncrement * (states.frames.size() + 1),
        Frame::kEmptySize
    });
    states.current_frame = &states.frames.back();
}

void EndFrame(Context context_) {
    WidgetStates& states = context_->current_record;

    if (!states.current_frame)
        return;

    states.current_frame = nullptr;
}

} // namespace guitk

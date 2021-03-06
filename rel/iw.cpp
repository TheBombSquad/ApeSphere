#include "iw.h"
#include "pad.h"
#include "assembly.h"

#include <mkb.h>
#include <cstring>
#include <cstdio>

#include "draw.h"
#include "patch.h"

// TODO: track best times per world
// I tried this before but it seems like there might be a spurious frame where it thinks the IW is completed
// when beating the second-to-last level, so the fastest time isn't saving correctly.

namespace iw
{

static bool s_enabled;
static u32 s_patch1, s_patch2;

static u32 s_anim_counter;
static char *s_anim_strs[4] = {"/", "-", "\\", " |"};

// IW timer stuff
static u32 s_iw_time;
static u32 s_prev_retrace_count;

void init()
{
    if (s_enabled) return;

    s_enabled = true;

    // IW-related patches
    s_patch1 = patch::write_branch(reinterpret_cast<void *>(0x80274804), reinterpret_cast<void *>(main::stage_select_menu_hook));
    s_patch2 = patch::write_branch(reinterpret_cast<void *>(0x8032a86c), reinterpret_cast<void *>(main::pause_menu_text_hook));
}

void dest()
{
    if (!s_enabled) return;

    s_enabled = false;
    patch::write_word(reinterpret_cast<void *>(0x80274804), s_patch1);
    patch::write_word(reinterpret_cast<void *>(0x8032a86c), s_patch2);
    mkb::strcpy(mkb::continue_saved_game_text, "Continue the game from the saved point.");
    mkb::strcpy(mkb::start_game_from_beginning_text, "Start the game from the beginning.");
}

bool is_enabled()
{
    return s_enabled;
}

static void handle_iw_selection()
{
    if (mkb::g_storymode_mode != 5) return;

    if (pad::analog_down(mkb::PAI_LSTICK_LEFT) || pad::analog_down(mkb::PAI_LSTICK_RIGHT)) return;
    if (pad::button_down(mkb::PAD_BUTTON_LEFT) || pad::button_down(mkb::PAD_BUTTON_RIGHT)) return;

    bool lstick_up = pad::analog_pressed(mkb::PAI_LSTICK_UP);
    bool lstick_down = pad::analog_pressed(mkb::PAI_LSTICK_DOWN);
    bool dpad_up = pad::button_pressed(mkb::PAD_BUTTON_UP);
    bool dpad_down = pad::button_pressed(mkb::PAD_BUTTON_DOWN);

    s32 dir = lstick_up || dpad_up ? +1 : (lstick_down || dpad_down ? -1 : 0);
    auto &story_save = mkb::storymode_save_files[mkb::selected_story_file_idx];
    if (story_save.is_valid)
    {
        s32 world = story_save.current_world + dir;
        if (world < 0 || world > 9) story_save.is_valid = 0;
        else story_save.current_world = world;
    }
    else
    {
        if (dir != 0)
        {
            story_save.is_valid = 1;
            story_save.current_world = dir == +1 ? 0 : 9;
        }
    }
}

static void set_save_file_info()
{
    if (mkb::g_storymode_mode != 5) return;

    s_anim_counter += 1;

    for (s32 i = 0; i < 3; i++)
    {
        auto &story_save = mkb::storymode_save_files[i];
        if (story_save.is_valid)
        {
            mkb::sprintf(story_save.file_name, "W%02d IW %s",
                    story_save.current_world + 1,
                    s_anim_strs[s_anim_counter / 2 % 4]);
            story_save.num_beaten_stages_in_current_world = 0;
            story_save.score = 0;
            story_save.playtime_in_frames = 0;
        }
    }
}

static void handle_iw_timer()
{
    u32 retrace_count = mkb::VIGetRetraceCount();

    // Halt the timer if we're selecting the story mode file
    // If we're still on the file selection screen and the IW file has been opened though,
    // start the timer during the open animation (to be more consistent with prior versions
    // of the IW code)
    if (mkb::g_storymode_mode == 5 && mkb::data_select_menu_state != mkb::DSMS_OPEN_DATA)
    {
        // We're not actually in the IW, zero the timer
        s_iw_time = 0;
    }
    else if (main::currently_playing_iw && !main::is_iw_complete())
    {
        // We're in story mode playing an IW and it isn't finished, so increment the IW timer
        s_iw_time += retrace_count - s_prev_retrace_count;
    }
    // Else we're in story mode playing an IW, but we finished it, so don't change the time

    s_prev_retrace_count = retrace_count;
}

void tick()
{
    if (!s_enabled) return;

    main::currently_playing_iw = false;
    if (mkb::main_mode != mkb::MD_GAME || mkb::main_game_mode != mkb::STORY_MODE) return;

    char *msg = "Up/Down to Change World.";
    mkb::strcpy(mkb::continue_saved_game_text, msg);
    mkb::strcpy(mkb::start_game_from_beginning_text, msg);

    handle_iw_selection();
    set_save_file_info();

    u8 file_idx = mkb::g_storymode_mode == 5
        ? mkb::selected_story_file_idx
        : mkb::curr_storymode_save_file_idx;

    // Maybe not the best way to detect if we're playing an IW but it works
    mkb::StoryModeSaveFile &file = mkb::storymode_save_files[file_idx];
    main::currently_playing_iw =
        file.is_valid
        && file.file_name[0] == 'W'
        && file.file_name[4] == 'I'
        && file.file_name[5] == 'W';

    handle_iw_timer();
}

void disp()
{
    if (!s_enabled
    || mkb::main_mode != mkb::MD_GAME
    || mkb::main_game_mode != mkb::STORY_MODE
    || !main::currently_playing_iw)
        return;

    constexpr u32 SECOND = 60;
    constexpr u32 MINUTE = SECOND * 60;
    constexpr u32 HOUR = MINUTE * 60;

    constexpr s32 X = 380;
    constexpr s32 Y = 18;

    u32 hours = s_iw_time / HOUR;
    u32 minutes = s_iw_time % HOUR / MINUTE;
    u32 seconds = s_iw_time % MINUTE / SECOND;
    u32 centiseconds = (s_iw_time % SECOND) * 100 / 60;

    if (hours > 0)
    {
        draw::debug_text(X, Y, draw::Color::White, "IW:  %d:%02d:%02d.%02d", hours, minutes, seconds, centiseconds);
    }
    else if (minutes > 0)
    {
        draw::debug_text(X, Y, draw::Color::White, "IW:  %02d:%02d.%02d", minutes, seconds, centiseconds);
    }
    else
    {
        draw::debug_text(X, Y, draw::Color::White, "IW:  %02d.%02d", seconds, centiseconds);
    }
}

}

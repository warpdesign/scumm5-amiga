/* scumm5-amiga - SCUMM v5 interpreter state
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Structures and limits follow ScummVM engines/scumm (scumm.h, script.h,
 * actor.h, object.h, verbs.h, charset.h), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. ScummVM
 * is the legal property of its developers, whose names are too numerous
 * to list here; see the COPYRIGHT file distributed with the ScummVM source.
 */
#ifndef SCUMM_H
#define SCUMM_H

#include "platform.h"
#include "res.h"

/* ---- limits ---- */
#define NUM_SCRIPT_SLOT 80
#define NUM_SCRIPT_LOCAL 25
#define MAX_NESTING 15
#define MAX_CUTSCENE 5
#define NUM_SENTENCE 6
#define NUM_ACTORS 13
#define NUM_LOCAL_OBJECTS 200
#define NUM_INVENTORY 80
#define NUM_VERBS 100
#define NUM_ARRAY 50
#define NUM_NEW_NAMES 150
#define NUM_GLOBAL_SCRIPTS 200
#define NUM_LOCAL_SCRIPTS 1024
#define NUM_FL_OBJECTS 50
#define NUM_CHARSETS 9
#define NUM_SCALE_SLOTS 20
#define NUM_STRING_SLOTS 6
#define NUM_COLOR_CYCLES 16
#define MAX_DRAW_QUEUE 200

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

#define SCRIPT_EXCD 10001
#define SCRIPT_ENCD 10002

enum { SS_DEAD = 0, SS_PAUSED = 1, SS_RUNNING = 2 };
enum { WIO_NOT_FOUND = -1, WIO_INVENTORY = 0, WIO_ROOM = 1, WIO_GLOBAL = 2, WIO_LOCAL = 3, WIO_FLOBJECT = 4 };

#define OF_OWNER_ROOM 0x0F

enum {
	CLS_NEVER_CLIP = 20,
	CLS_ALWAYS_CLIP = 21,
	CLS_IGNORE_BOXES = 22,
	CLS_YFLIP = 29,
	CLS_XFLIP = 30,
	CLS_PLAYER = 31,
	CLS_UNTOUCHABLE = 32
};

/* ---- variables (v5 numbering) ---- */
enum {
	VAR_KEYPRESS = 0, VAR_EGO = 1, VAR_CAMERA_POS_X = 2, VAR_HAVE_MSG = 3,
	VAR_ROOM = 4, VAR_OVERRIDE = 5, VAR_MACHINE_SPEED = 6, VAR_ME = 7,
	VAR_NUM_ACTOR = 8, VAR_CURRENT_LIGHTS = 9, VAR_CURRENTDRIVE = 10,
	VAR_TMR_1 = 11, VAR_TMR_2 = 12, VAR_TMR_3 = 13, VAR_MUSIC_TIMER = 14,
	VAR_ACTOR_RANGE_MIN = 15, VAR_ACTOR_RANGE_MAX = 16,
	VAR_CAMERA_MIN_X = 17, VAR_CAMERA_MAX_X = 18, VAR_TIMER_NEXT = 19,
	VAR_VIRT_MOUSE_X = 20, VAR_VIRT_MOUSE_Y = 21, VAR_ROOM_RESOURCE = 22,
	VAR_LAST_SOUND = 23, VAR_CUTSCENEEXIT_KEY = 24, VAR_TALK_ACTOR = 25,
	VAR_CAMERA_FAST_X = 26, VAR_SCROLL_SCRIPT = 27, VAR_ENTRY_SCRIPT = 28,
	VAR_ENTRY_SCRIPT2 = 29, VAR_EXIT_SCRIPT = 30, VAR_EXIT_SCRIPT2 = 31,
	VAR_VERB_SCRIPT = 32, VAR_SENTENCE_SCRIPT = 33, VAR_INVENTORY_SCRIPT = 34,
	VAR_CUTSCENE_START_SCRIPT = 35, VAR_CUTSCENE_END_SCRIPT = 36,
	VAR_CHARINC = 37, VAR_WALKTO_OBJ = 38, VAR_DEBUGMODE = 39,
	VAR_HEAPSPACE = 40, VAR_RESTART_KEY = 42, VAR_PAUSE_KEY = 43,
	VAR_MOUSE_X = 44, VAR_MOUSE_Y = 45, VAR_TIMER = 46, VAR_TIMER_TOTAL = 47,
	VAR_SOUNDCARD = 48, VAR_VIDEOMODE = 49, VAR_MAINMENU_KEY = 50,
	VAR_FIXEDDISK = 51, VAR_CURSORSTATE = 52, VAR_USERPUT = 53,
	VAR_V5_TALK_STRING_Y = 54, VAR_SOUNDRESULT = 56, VAR_TALKSTOP_KEY = 57,
	VAR_FADE_DELAY = 59, VAR_NOSUBTITLES = 60, VAR_SOUNDPARAM = 64,
	VAR_SOUNDPARAM2 = 65, VAR_SOUNDPARAM3 = 66, VAR_INPUTMODE = 67,
	VAR_MEMORY_PERFORMANCE = 68, VAR_VIDEO_PERFORMANCE = 69,
	VAR_ROOM_FLAG = 70, VAR_GAME_LOADED = 71, VAR_NEW_ROOM = 72
};

/* ---- scripts ---- */
struct script_slot {
	uint32_t offs;
	int32_t delay;
	uint16_t number;
	uint16_t delay_frame_count;
	uint8_t status;
	uint8_t where;
	uint8_t freeze_resistant;
	uint8_t recursive;
	uint8_t freeze_count;
	uint8_t didexec;
	uint8_t cutscene_override;
	uint8_t cycle;
};

struct nested_script {
	uint16_t number;
	uint8_t where;
	uint8_t slot;
};

struct sentence {
	uint8_t verb;
	uint8_t preposition;
	uint16_t object_a;
	uint16_t object_b;
	uint8_t freeze_count;
};

/* ---- objects ---- */
struct object_data {
	uint32_t obim_offset;
	uint32_t obcd_offset;
	int16_t walk_x, walk_y;
	uint16_t obj_nr;
	int16_t x_pos, y_pos;
	uint16_t width, height;
	uint8_t actordir;
	uint8_t parent;
	uint8_t parentstate;
	uint8_t state;
	uint8_t fl_object_index;
	uint8_t flags;
};

/* ---- actors ---- */
struct point {
	int16_t x, y;
};

struct cost_state {
	uint16_t anim_counter;
	uint8_t sound_counter;
	uint8_t sound_pos;
	uint16_t stopped;
	uint16_t curpos[16];
	uint16_t start[16];
	uint16_t end[16];
	uint16_t frame[16];
};

struct walk_data {
	struct point dest;
	uint8_t destbox;
	int16_t destdir;
	struct point cur;
	struct point next;
	struct point point3;
	int32_t delta_x_factor, delta_y_factor;
	uint16_t xfrac, yfrac;
	uint8_t curbox;
};

struct actor {
	uint8_t number;
	struct point pos;
	int32_t top, bottom;
	uint16_t width;
	uint8_t room;
	uint8_t talk_color;
	int16_t talk_pos_x, talk_pos_y;
	uint8_t talk_script, walk_script;
	uint8_t ignore_boxes;
	uint8_t force_clip;
	uint8_t init_frame, walk_frame, stand_frame, talk_start_frame, talk_stop_frame;
	uint8_t needs_redraw, need_bg_reset, costume_needs_init, visible;
	uint8_t shadow_mode, flip;
	uint8_t frame;
	uint8_t walkbox;
	int16_t elevation;
	uint16_t costume;
	uint8_t moving;
	uint8_t ignore_turns;
	uint8_t boxscale;
	uint8_t scalex, scaley;
	uint8_t charset;
	uint8_t anim_speed, anim_progress;
	int16_t facing, target_facing;
	uint16_t speedx, speedy;
	int16_t last_valid_x, last_valid_y;
	uint8_t sound[1];
	uint8_t palette[32];
	struct walk_data walkdata;
	struct cost_state cost;
	uint8_t *name;		/* res_alloc'd, or NULL */
};

#define MF_NEW_LEG 1
#define MF_IN_LEG 2
#define MF_TURN 4
#define MF_LAST_LEG 8

/* ---- verbs ---- */
struct rect {
	int16_t left, top, right, bottom;
};

struct verb_slot {
	struct rect cur_rect;
	struct rect old_rect;
	uint16_t verbid;
	uint8_t color, hicolor, dimcolor, bkcolor, type;
	uint8_t charset_nr, curmode;
	uint16_t saveid;
	uint8_t key, center;
	uint8_t prep;
	uint16_t imgindex;
	int16_t orig_left;
	uint8_t *name;		/* res_alloc'd text, or NULL */
};

enum { VERB_TEXT = 0, VERB_IMAGE = 1 };

/* ---- text ---- */
struct string_tab_part {
	int16_t xpos, ypos;
	int16_t right;
	int16_t height;
	uint8_t color;
	uint8_t charset;
	uint8_t center;
	uint8_t overhead;
	uint8_t no_talk_anim;
	uint8_t wrapping;
};

struct string_tab {
	struct string_tab_part cur;
	struct string_tab_part def;	/* "_default" */
};

/* ---- camera ---- */
struct camera {
	struct point cur, dest, accel, last;
	int16_t left_trigger, right_trigger;
	uint8_t follows;
	uint8_t mode;
	uint8_t moving_to_actor;
};

enum { CAMERA_NORMAL = 1, CAMERA_FOLLOW_ACTOR = 2, CAMERA_PANNING = 3 };

struct scale_slot {
	int16_t x1, y1, scale1;
	int16_t x2, y2, scale2;
};

struct color_cycle {
	uint16_t delay;
	uint16_t counter;
	uint16_t flags;
	uint8_t start;
	uint8_t end;
};

/* ---- the engine ---- */
struct scumm {
	/* scripts */
	struct script_slot slot[NUM_SCRIPT_SLOT];
	int32_t localvar[NUM_SCRIPT_SLOT][NUM_SCRIPT_LOCAL + 1];
	struct nested_script nest[MAX_NESTING];
	uint8_t num_nested;
	uint8_t cutscene_sp;
	uint8_t cutscene_script_index;
	uint32_t cutscene_ptr[MAX_CUTSCENE];
	uint8_t cutscene_script[MAX_CUTSCENE];
	int16_t cutscene_data[MAX_CUTSCENE];

	uint8_t current_script;	/* 0xFF: none */
	const uint8_t *script_org;
	const uint8_t *script_ptr;
	uint8_t opcode;
	uint16_t result_var;
	int32_t stack[150];
	int stack_pos;

	int32_t *vars;
	uint16_t num_vars;
	uint8_t *bit_vars;
	uint16_t num_bit_vars;

	struct sentence sentences[NUM_SENTENCE];
	uint8_t sentence_num;

	/* objects */
	uint16_t num_global_objects;
	uint8_t *object_owner;
	uint8_t *object_state;
	uint32_t *class_data;
	struct object_data objs[NUM_LOCAL_OBJECTS];
	uint16_t inventory[NUM_INVENTORY];
	uint8_t *inventory_data[NUM_INVENTORY];
	uint8_t *fl_objects[NUM_FL_OBJECTS];
	uint16_t new_names[NUM_NEW_NAMES];
	uint8_t *new_name_data[NUM_NEW_NAMES];
	uint16_t draw_queue[MAX_DRAW_QUEUE];
	uint16_t draw_queue_nr;
	uint8_t bg_needs_redraw;

	/* rooms */
	uint8_t current_room;
	uint8_t room_resource;
	uint8_t resource_mapper[128];
	uint16_t room_width, room_height;
	uint8_t num_objects_in_room;
	uint32_t encd_offs, excd_offs, clut_offs;
	uint32_t local_script_offsets[NUM_LOCAL_SCRIPTS];
	uint8_t *box_data;		/* writable copy of BOXD */
	uint32_t box_data_size;
	uint8_t *box_matrix;		/* writable copy of BOXM */
	uint32_t box_matrix_size;
	uint16_t extra_box_flags[65];
	struct scale_slot scale_slots[NUM_SCALE_SLOTS];
	uint8_t room_palette[256];
	uint8_t shadow_palette[256];
	uint8_t current_palette[768];
	struct color_cycle color_cycle[NUM_COLOR_CYCLES];
	uint8_t switch_room_effect, switch_room_effect2, new_effect;
	uint8_t full_redraw;
	uint8_t ego_positioned;
	uint8_t screen_start_strip, screen_end_strip;
	int16_t screen_top;

	/* actors and camera */
	struct actor actors[NUM_ACTORS];
	struct camera camera;
	uint8_t talk_delay_pending;
	int32_t talk_delay;
	uint8_t actor_to_print_str_for;
	uint8_t have_msg;
	uint8_t have_actor_speech_msg;
	uint8_t keep_text;
	uint8_t talking_actor;

	/* verbs, cursor, input */
	struct verb_slot verbs[NUM_VERBS];
	int8_t cursor_state;
	int8_t user_put;
	uint8_t current_cursor;
	struct point mouse, virtual_mouse;
	uint16_t mouse_and_keyboard_stat;
	uint8_t left_btn_pressed, right_btn_pressed;
	uint8_t verb_mouse_over;

	/* text */
	struct string_tab string[NUM_STRING_SLOTS];
	uint8_t *strings[NUM_ARRAY];	/* rtString resources */
	uint16_t string_size[NUM_ARRAY];
	uint8_t charset_color_map[16];
	uint8_t charset_data[NUM_CHARSETS][16];
	uint8_t cur_charset;
	uint8_t charset_buf[512];
	uint16_t charset_buf_pos;
	uint8_t *message_ptr;

	/* sound */
	uint16_t sound_queue[16];
	uint8_t sound_queue_pos;

	uint32_t rnd_seed;
	uint32_t frame;
	uint8_t quit;
};

extern struct scumm g;

/* ---- vm.c ---- */
void vm_init(void);
int32_t read_var(uint32_t var);
void write_var(uint32_t var, int32_t value);
#define VAR(v) (g.vars[v])
uint8_t fetch_byte(void);
uint16_t fetch_word(void);
int16_t fetch_word_signed(void);
void run_script(int script, int freeze_resistant, int recursive, const int32_t *lvars);
void run_object_script(int object, int entry, int freeze_resistant, int recursive, const int32_t *vars);
void stop_script(int script);
void stop_object_script(int script);
void stop_object_code(void);
void run_all_scripts(void);
void run_exit_script(void);
void run_entry_script(void);
void kill_scripts_and_resources(void);
void freeze_scripts(int flag);
void unfreeze_scripts(void);
void decrease_script_delay(int amount);
int is_script_in_use(int script);
int is_script_running(int script);
int is_room_script_running(int script);
void begin_cutscene(const int32_t *args);
void end_cutscene(void);
void abort_cutscene(void);
void begin_override(void);
void end_override(void);
void do_sentence(int verb, int a, int b);
void check_and_run_sentence_script(void);
void run_input_script(int click_area, int val, int mode);
void run_inventory_script(int i);
int get_verb_entrypoint(int obj, int entry);
int res_str_len(const uint8_t *src);
void update_script_ptr(void);
void refresh_script_ptr(void);
int get_script_slot(void);
void run_script_nested(int slot);
uint32_t rnd_get(uint32_t max);

/* ---- ops5.c ---- */
void execute_opcode(uint8_t op);
const char *opcode_name(uint8_t op);
int get_word_vararg(int32_t *ptr);

/* ---- object.c ---- */
int get_object_index(int object);
int where_is_object(int object);
int get_state(int obj);
void put_state(int obj, int state);
int get_owner(int obj);
void put_owner(int obj, int owner);
int get_class(int obj, int cls);
void put_class(int obj, int cls, int set);
void set_owner_of(int obj, int owner);
void add_object_to_inventory(int obj, int room);
int find_inventory(int owner, int idx);
int get_inventory_count(int owner);
int find_object(int x, int y);
int get_obj_x(int obj);
int get_obj_y(int obj);
int get_object_or_actor_xy(int object, int *x, int *y);
void get_object_xy_pos(int object, int *x, int *y, int *dir);
int get_obj_act_to_obj_act_dist(int a, int b);
const uint8_t *get_obcd_from_object(int obj);
uint32_t get_obcd_offs(int object);
void clear_room_objects(void);
void reset_room_objects(void);
void update_object_states(void);
void add_object_to_draw_queue(int idx);
void clear_draw_object_queue(void);
void mark_object_rect_as_dirty(int obj);
void set_object_name(int obj);
const uint8_t *get_obj_or_actor_name(int obj);
void load_fl_object(int object, int room);
void read_global_objects(const uint8_t *dobj);
uint8_t *script_base(const struct script_slot *ss);

/* ---- room.c ---- */
void start_scene(int room, struct actor *a, int object);
void set_pal_color(int idx, int r, int g, int b);
void darken_palette(int red, int green, int blue, int start, int end);

/* ---- boxes.c ---- */
int get_num_boxes(void);
int get_box_flags(int box);
void set_box_flags(int box, int val);
int get_box_scale(int box);
void set_box_scale(int box, int scale);
int get_scale(int box, int x, int y);
void set_scale_slot(int slot, int x1, int y1, int scale1, int x2, int y2, int scale2);
int check_xy_in_box_bounds(int box, int x, int y);
struct box_coords { struct point ul, ur, ll, lr; };
struct box_coords get_box_coordinates(int box);
int get_closest_pt_on_box(const struct box_coords *box, int x, int y, int16_t *ox, int16_t *oy);
int get_next_box(uint8_t from, uint8_t to);
void create_box_matrix(void);

/* ---- actor.c ---- */
#define INVALID_BOX 0
struct actor *deref_actor(int id);
struct actor *deref_actor_safe(int id);
int is_valid_actor(int id);
void init_actor(struct actor *a, int mode);
void put_actor(struct actor *a, int x, int y, int room);
void put_actor_here(struct actor *a);
void hide_actor(struct actor *a);
void show_actor(struct actor *a);
void show_actors(void);
void walk_actors(void);
void start_walk_actor(struct actor *a, int x, int y, int dir);
void stop_actor_moving(struct actor *a);
void set_actor_walk_speed(struct actor *a, int sx, int sy);
void set_actor_costume(struct actor *a, int c);
void set_direction(struct actor *a, int dir);
void turn_to_direction(struct actor *a, int dir);
void face_to_object(struct actor *a, int obj);
void animate_actor(struct actor *a, int anim);
void start_anim_actor(struct actor *a, int frame);
void set_actor_scale(struct actor *a, int sx, int sy);
void set_actor_elevation(struct actor *a, int e);
void set_actor_palette(struct actor *a, int idx, int val);
int is_in_current_room(const struct actor *a);
struct adjust_box_result { int16_t x, y; uint8_t box; };
struct adjust_box_result adjust_xy_to_be_in_box(struct actor *a, int x, int y);
int get_actor_from_pos(int x, int y);
void actor_talk(const uint8_t *msg);
void stop_talk(void);
int get_talking_actor(void);
void set_talking_actor(int i);
void play_actor_sounds(void);
int new_dir_to_old_dir(int dir);
int old_dir_to_new_dir(int dir);
void walk_actor_to_actor(int actor, int to_actor, int dist);
void actor_follow_camera(int act);

/* ---- costume.c ---- */
void costume_decode_data(struct actor *a, int frame, unsigned usemask);
int costume_increase_anims(struct actor *a);
int costume_draw_extents(struct actor *a);

/* ---- strip usage bits (actor.c) ---- */
#define USAGE_BIT_RESTORED 95
#define USAGE_BIT_DIRTY 96
void set_gfx_usage_bit(int strip, int bit);
void clear_gfx_usage_bits(void);
void mark_rect_as_dirty(int left, int right, int top, int bottom, int dirtybit);
void process_actors(void);

/* ---- camera.c ---- */
void set_camera_at(int pos_x, int pos_y);
void set_camera_at_ex(int at);
void set_camera_follows(struct actor *a, int set_camera);
void pan_camera_to(int x, int y);
void move_camera(void);
void camera_moved(void);

/* ---- verbs.c ---- */
int get_verb_slot(int id, int mode);
void kill_verb(int slot);
void draw_verb(int verb, int mode);
void verb_mouse_over(int verb);
void check_exec_verbs(void);
void handle_mouse_over(int update_inventory);
void set_verb_object(int room, int object, int verb);
void redraw_verbs(void);

/* ---- text.c ---- */
void load_charset(int no);
void init_charset(int no);
int charset_string_width(int charset, const uint8_t *str);
void print_string(int m, const uint8_t *msg);
void display_dialog(void);
void load_ptr_to_string(int idx, const uint8_t *src);
void nuke_string(int idx);
uint8_t *create_string(int idx, int size);
void redefine_builtin_cursor_from_char(int idx, int chr);
void redefine_builtin_cursor_hotspot(int idx, int x, int y);

/* ---- sound.c ---- */
void sound_start(int sound);
void sound_stop(int sound);
void sound_stop_all(void);
int sound_is_running(int sound);
void sound_kludge(const int32_t *items, int num);
void sound_process(void);
void sound_update_music_timer(void);

/* ---- input.c ---- */
void process_input(void);
int playback_open(const char *path);
void playback_pump(void);

/* ---- engine.c ---- */
int engine_init(const char *pack, uint32_t arena);
void engine_boot(void);
void scumm_loop(int delta);
void engine_frame(void);

/* ---- misc ---- */
void init_screens(int b, int h);
int find_virt_screen(int y);
void fade_in(int effect);
void clear_clicked_status(void);

/* ---- trace.c ---- */
extern int trace_on;
void trace_opcode(void);
void trace_run_script(int script, int local);
void trace_text(const char *s);
void trace_open(const char *path);
void trace_flush(void);
void trace_close(void);
void fatal(const char *msg, int a, int b);

#endif

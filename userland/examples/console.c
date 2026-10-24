#include <stddef.h>
#include <stdint.h>
#include "chos/user.h"

#define CONSOLE_LINES 11u
#define CONSOLE_LINE_WIDTH 72u
#define CONSOLE_INPUT_CAPACITY 96u
#define CONSOLE_READ_LIMIT 2048u
#define CONSOLE_PATH_CAPACITY CHOS_FILE_PATH_CAPACITY
#define GUI_WINDOW_MIN_ID 100l
#define GUI_WINDOW_MAX_ID 103l

static const char app_title[] = "CHOS Console";
static char output_lines[CONSOLE_LINES][CONSOLE_LINE_WIDTH + 1u];
static size_t output_count = 1u;
static size_t output_column;
static char input_line[CONSOLE_INPUT_CAPACITY];
static size_t input_length;
static uint32_t window_id;

static void message_begin(chos_gui_message_t *message, uint16_t type, size_t payload_size) {
    chos_memset(message, 0, sizeof(*message));
    message->header.version = CHOS_GUI_ABI_VERSION;
    message->header.type = type;
    message->header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + payload_size);
}

static long submit(chos_gui_message_t *message) {
    return chos_gui_submit(message, message->header.size_bytes);
}

static void newline(void) {
    size_t row;
    if (output_count < CONSOLE_LINES) {
        ++output_count;
    } else {
        for (row = 1u; row < CONSOLE_LINES; ++row)
            chos_memcpy(output_lines[row - 1u], output_lines[row], sizeof(output_lines[row]));
        chos_memset(output_lines[CONSOLE_LINES - 1u], 0, sizeof(output_lines[0]));
    }
    output_column = 0u;
}

static void put_char(char ch) {
    if (ch == '\r') return;
    if (ch == '\n') { newline(); return; }
    if ((unsigned char)ch < 32u && ch != '\t') return;
    if (ch == '\t') {
        do { put_char(' '); } while ((output_column & 3u) != 0u);
        return;
    }
    if (output_column >= CONSOLE_LINE_WIDTH) newline();
    output_lines[output_count - 1u][output_column++] = ch;
    output_lines[output_count - 1u][output_column] = '\0';
}

static void write_text(const char *text) {
    while (*text != '\0') put_char(*text++);
}

static void write_unsigned(uint64_t value) {
    char digits[21]; size_t length = 0u;
    do { digits[length++] = (char)('0' + (value % 10u)); value /= 10u; } while (value != 0u);
    while (length > 0u) put_char(digits[--length]);
}

static int draw_text(uint32_t id, int32_t x, int32_t y, const char *text, uint32_t argb) {
    chos_gui_message_t message;
    size_t length = chos_strlen(text);
    if (length >= CHOS_GUI_MAX_TEXT) return 0;
    message_begin(&message, CHOS_GUI_MSG_DRAW_TEXT, sizeof(message.payload.draw_text));
    message.payload.draw_text.window_id = id;
    message.payload.draw_text.x = x;
    message.payload.draw_text.y = y;
    message.payload.draw_text.argb = argb;
    message.payload.draw_text.text_length = (uint16_t)length;
    chos_memcpy(message.payload.draw_text.text, text, length);
    return submit(&message) == 0;
}

static void redraw(void) {
    chos_gui_message_t message;
    size_t i;
    message_begin(&message, CHOS_GUI_MSG_CLEAR, sizeof(message.payload.clear));
    message.payload.clear.window_id = window_id;
    message.payload.clear.background_argb = 0xff101820u;
    if (submit(&message) != 0) return;
    (void)draw_text(window_id, 12, 8, "CHOS user-space Console", 0xff80c0ffu);
    for (i = 0u; i < output_count; ++i)
        (void)draw_text(window_id, 12, 34 + (int32_t)i * 21, output_lines[i], 0xffe0e0e0u);
    {
        char prompt[CONSOLE_INPUT_CAPACITY + 8u];
        size_t n = 0u;
        const char prefix[] = "chos> ";
        for (i = 0u; i < sizeof(prefix) - 1u; ++i) prompt[n++] = prefix[i];
        for (i = 0u; i < input_length && n + 1u < sizeof(prompt); ++i) prompt[n++] = input_line[i];
        prompt[n] = '\0';
        (void)draw_text(window_id, 12, 294, prompt, 0xff80ff80u);
        (void)draw_text(window_id, 12, 322, "help dir mkdir touch write rename rm rmdir sync type run clear exit", 0xffa0a0a0u);
    }
}


static void drain_session_output(void) {
    char bytes[CHOS_SYS_MAX_READ];
    long got = chos_console_read(bytes, sizeof(bytes));
    size_t i;
    if (got <= 0) return;
    for (i = 0u; i < (size_t)got; ++i) put_char(bytes[i]);
    redraw();
}

static int strings_equal(const char *a, const char *b) {
    size_t i = 0u;
    while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
    return a[i] == '\0' && b[i] == '\0';
}

static void command_help(void) {
    write_text("Available commands:"); newline();
    write_text("  help              show this help"); newline();
    write_text("  dir               list merged CHFS/ROMFS files"); newline();
    write_text("  sysinfo            show process/runtime info"); newline();
    write_text("  type /PATH         display a text file"); newline();
    write_text("  run /BIN/APP.CHX   launch an embedded application"); newline();
    write_text("  mkdir PATH         create a directory"); newline();
    write_text("  touch PATH         create an empty file"); newline();
    write_text("  write PATH TEXT    create/replace a text file"); newline();
    write_text("  rename OLD NEW     rename a persistent file"); newline();
    write_text("  rm PATH            delete a file"); newline();
    write_text("  rmdir PATH         remove an empty directory"); newline();
    write_text("  sync               flush persistent storage"); newline();
    write_text("  clear              clear the scrollback"); newline();
    write_text("  exit               close this Console window"); newline();
}

static void command_dir(const char *path) {
    chos_file_list_entry_t files[CHOS_FILE_LIST_MAX];
    long count = chos_list_dir(path == NULL || *path == '\0' ? "/" : path, files, CHOS_FILE_LIST_MAX);
    size_t i;
    if (count < 0) { write_text("dir: file catalog unavailable"); newline(); return; }
    if (count == 0) { write_text("No files found."); newline(); return; }
    write_text("Path                                                        Bytes"); newline();
    for (i = 0u; i < (size_t)count; ++i) {
        write_text((files[i].flags & CHOS_FILE_FLAG_DIRECTORY) != 0u ? "[DIR]  " : "       ");
        write_text(files[i].path); write_text("  "); write_unsigned(files[i].size_bytes);
        if ((files[i].flags & CHOS_FILE_FLAG_EXECUTABLE) != 0u) write_text(" [program]");
        newline();
    }
}

static void command_type(const char *path) {
    char buffer[CHOS_SYS_MAX_READ + 1u];
    long fd;
    size_t total = 0u;
    if (path == NULL || *path == '\0') { write_text("usage: type /PATH"); newline(); return; }
    fd = chos_open(path);
    if (fd < 0) { write_text("type: cannot open file"); newline(); return; }
    while (total < CONSOLE_READ_LIMIT) {
        long got = chos_read((int)fd, buffer, CHOS_SYS_MAX_READ);
        size_t i;
        if (got < 0) { write_text("type: read error"); newline(); break; }
        if (got == 0) break;
        for (i = 0u; i < (size_t)got; ++i) put_char(buffer[i]);
        total += (size_t)got;
    }
    if (total == CONSOLE_READ_LIMIT) { newline(); write_text("[output truncated at 2 KiB]"); newline(); }
    (void)chos_close((int)fd);
    if (output_column != 0u) newline();
}

static const char *skip_spaces(const char *text) {
    while (text != NULL && (*text == ' ' || *text == '\t')) ++text;
    return text;
}

static void command_file_error(const char *operation, long result) {
    write_text(operation); write_text(": failed (error ");
    write_unsigned((uint64_t)(result < 0 ? -result : CHOS_ERRNO_EIO));
    put_char(')'); newline();
}

static void command_mkdir(const char *path) {
    long result = (*path == '\0') ? -(long)CHOS_ERRNO_EINVAL : chos_mkdir(path);
    if (result < 0) command_file_error("mkdir", result);
    else { write_text("Directory created."); newline(); }
}

static void command_touch(const char *path) {
    long result = (*path == '\0') ? -(long)CHOS_ERRNO_EINVAL : chos_write_file(path, "", 0u);
    if (result < 0) command_file_error("touch", result);
    else { write_text("Empty file created."); newline(); }
}

static void command_write_file(const char *arguments) {
    char path[CONSOLE_PATH_CAPACITY];
    size_t n = 0u;
    const char *text;
    long result;
    arguments = skip_spaces(arguments);
    while (arguments[n] != '\0' && arguments[n] != ' ' && arguments[n] != '\t' && n + 1u < sizeof(path)) {
        path[n] = arguments[n]; ++n;
    }
    path[n] = '\0';
    if (n == 0u || (arguments[n] != ' ' && arguments[n] != '\t')) {
        write_text("usage: write /PATH TEXT"); newline(); return;
    }
    text = skip_spaces(arguments + n);
    result = chos_write_file(path, text, chos_strlen(text));
    if (result < 0) command_file_error("write", result);
    else { write_text("Wrote "); write_unsigned((uint64_t)result); write_text(" bytes."); newline(); }
}

static void command_rename(const char *arguments) {
    char old_path[CONSOLE_PATH_CAPACITY], new_path[CONSOLE_PATH_CAPACITY];
    size_t n = 0u, m = 0u;
    long result;
    arguments = skip_spaces(arguments);
    while (arguments[n] != '\0' && arguments[n] != ' ' && arguments[n] != '\t' && n + 1u < sizeof(old_path)) {
        old_path[n] = arguments[n]; ++n;
    }
    old_path[n] = '\0';
    if (n == 0u || (arguments[n] != ' ' && arguments[n] != '\t')) {
        write_text("usage: rename /OLD/PATH /NEW/PATH"); newline(); return;
    }
    arguments = skip_spaces(arguments + n);
    while (arguments[m] != '\0' && arguments[m] != ' ' && arguments[m] != '\t' && m + 1u < sizeof(new_path)) {
        new_path[m] = arguments[m]; ++m;
    }
    new_path[m] = '\0';
    if (m == 0u || arguments[m] != '\0') {
        write_text("usage: rename /OLD/PATH /NEW/PATH"); newline(); return;
    }
    result = chos_rename(old_path, new_path);
    if (result < 0) command_file_error("rename", result);
    else { write_text("File renamed."); newline(); }
}

static int execute_command(void) {
    char command[CONSOLE_INPUT_CAPACITY];
    size_t i = 0u, arg_start;
    while (i < input_length && input_line[i] != ' ' && input_line[i] != '\t') { command[i] = input_line[i]; ++i; }
    command[i] = '\0';
    arg_start = i;
    while (arg_start < input_length && (input_line[arg_start] == ' ' || input_line[arg_start] == '\t')) ++arg_start;
    input_line[input_length] = '\0';
    if (input_length == 0u) { newline(); return 0; }
    write_text("chos> "); write_text(input_line); newline();

    if (strings_equal(command, "help")) command_help();
    else if (strings_equal(command, "dir") || strings_equal(command, "ls")) command_dir(input_line + arg_start);
    else if (strings_equal(command, "mkdir")) command_mkdir(input_line + arg_start);
    else if (strings_equal(command, "touch")) command_touch(input_line + arg_start);
    else if (strings_equal(command, "write")) command_write_file(input_line + arg_start);
    else if (strings_equal(command, "rename")) command_rename(input_line + arg_start);
    else if (strings_equal(command, "rm")) {
        long result = chos_unlink(input_line + arg_start);
        if (result < 0) command_file_error("rm", result); else { write_text("File removed."); newline(); }
    } else if (strings_equal(command, "rmdir")) {
        long result = chos_rmdir(input_line + arg_start);
        if (result < 0) command_file_error("rmdir", result); else { write_text("Directory removed."); newline(); }
    } else if (strings_equal(command, "sync")) {
        long result = chos_fsync();
        if (result < 0) command_file_error("sync", result); else { write_text("Storage flushed."); newline(); }
    } else if (strings_equal(command, "sysinfo")) {
        write_text("CHOS 0.15.0-dev | ARM64 | EL0 application"); newline();
        write_text("PID: "); write_unsigned((uint64_t)chos_getpid()); newline();
        write_text("GUI ABI: "); write_unsigned(CHOS_GUI_ABI_VERSION); newline();
    } else if (strings_equal(command, "type")) command_type(input_line + arg_start);
    else if (strings_equal(command, "run")) {
        long pid;
        const char *path = input_line + arg_start;
        if (*path == '\0') { write_text("usage: run /BIN/APP.CHX"); newline(); }
        else {
            pid = chos_spawn(path);
            if (pid < 0) { write_text("run: launch failed (error "); write_unsigned((uint64_t)(-pid)); put_char(')'); newline(); }
            else { write_text("Started "); write_text(path); write_text(" (PID "); write_unsigned((uint64_t)pid); write_text(")"); newline(); }
        }
    } else if (strings_equal(command, "clear") || strings_equal(command, "cls")) {
        chos_memset(output_lines, 0, sizeof(output_lines)); output_count = 1u; output_column = 0u;
    } else if (strings_equal(command, "exit")) return 1;
    else { write_text("Unknown command. Type 'help'."); newline(); }
    return 0;
}

static char key_to_ascii(uint32_t keycode, int shift, int caps_lock) {
    char c = '\0';
    if (keycode >= 2u && keycode <= 11u) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        c = shift ? shifted[keycode - 2u] : normal[keycode - 2u];
    } else if (keycode >= 16u && keycode <= 25u) {
        static const char keys[] = "qwertyuiop"; c = keys[keycode - 16u];
    } else if (keycode >= 30u && keycode <= 38u) {
        static const char keys[] = "asdfghjkl"; c = keys[keycode - 30u];
    } else if (keycode >= 44u && keycode <= 50u) {
        static const char keys[] = "zxcvbnm"; c = keys[keycode - 44u];
    } else if (keycode == 57u) c = ' ';
    else if (keycode == 12u) c = shift ? '_' : '-';
    else if (keycode == 13u) c = shift ? '+' : '=';
    else if (keycode == 26u) c = shift ? '{' : '[';
    else if (keycode == 27u) c = shift ? '}' : ']';
    else if (keycode == 39u) c = shift ? ':' : ';';
    else if (keycode == 40u) c = shift ? '"' : '\'';
    else if (keycode == 41u) c = shift ? '~' : '`';
    else if (keycode == 43u || keycode == 86u) c = shift ? '|' : '\\';
    else if (keycode == 51u) c = shift ? '<' : ',';
    else if (keycode == 52u) c = shift ? '>' : '.';
    else if (keycode == 53u) c = shift ? '?' : '/';
    if ((shift != 0) != (caps_lock != 0) && c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    return c;
}

int main(int argc, char **argv) {
    chos_gui_message_t message, event;
    long result;
    (void)argc; (void)argv;
    message_begin(&message, CHOS_GUI_MSG_CREATE_WINDOW, sizeof(message.payload.create_window));
    message.payload.create_window.x = 90;
    message.payload.create_window.y = 70;
    message.payload.create_window.width = 640u;
    message.payload.create_window.height = 400u;
    message.payload.create_window.background_argb = 0xff101820u;
    result = submit(&message);
    if (result < GUI_WINDOW_MIN_ID || result > GUI_WINDOW_MAX_ID) chos_exit(1);
    window_id = (uint32_t)result;

    message_begin(&message, CHOS_GUI_MSG_SET_TITLE, sizeof(message.payload.set_title));
    message.payload.set_title.window_id = window_id;
    message.payload.set_title.title_length = (uint16_t)(sizeof(app_title) - 1u);
    chos_memcpy(message.payload.set_title.title, app_title, sizeof(app_title) - 1u);
    if (submit(&message) != 0) chos_exit(2);
    write_text("CHOS 0.15.0-dev user-space Console. Type 'help' for commands."); newline();
    write_text("This terminal runs as an ordinary EL0 application."); newline();
    message_begin(&message, CHOS_GUI_MSG_SHOW_WINDOW, sizeof(message.payload.window));
    message.payload.window.window_id = window_id;
    if (submit(&message) != 0) chos_exit(3);
    redraw();

    for (;;) {
        /* Child applications and kernel services write into a bounded Console
         * session ring. Drain it even when no keyboard event arrives. */
        drain_session_output();
        result = chos_gui_poll_event(&event, sizeof(event));
        if (result == -(long)CHOS_ERRNO_EAGAIN) { (void)chos_sleep_ticks(1u); continue; }
        if (result < 0) break;
        if (event.header.type == CHOS_GUI_MSG_CLOSE_REQUEST) break;
        if (event.header.type != CHOS_GUI_MSG_KEY_EVENT ||
            (event.payload.key_event.event_flags & CHOS_GUI_EVENT_KEY_DOWN) == 0u) continue;
        if (event.payload.key_event.keycode == 1u) break;
        if (event.payload.key_event.keycode == 14u) {
            if (input_length > 0u) input_line[--input_length] = '\0';
            redraw(); continue;
        }
        if (event.payload.key_event.keycode == 28u) {
            input_line[input_length] = '\0';
            if (execute_command()) break;
            chos_memset(input_line, 0, sizeof(input_line)); input_length = 0u;
            redraw(); continue;
        }
        if (input_length + 1u < sizeof(input_line)) {
            char c = key_to_ascii(event.payload.key_event.keycode,
                                  (event.payload.key_event.modifiers & 1u) != 0u,
                                  (event.payload.key_event.modifiers & 4u) != 0u);
            if (c != '\0') { input_line[input_length++] = c; input_line[input_length] = '\0'; redraw(); }
        }
    }
    message_begin(&message, CHOS_GUI_MSG_DESTROY_WINDOW, sizeof(message.payload.window));
    message.payload.window.window_id = window_id;
    (void)submit(&message);
    chos_exit(0);
}

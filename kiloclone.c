/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define KC_VERSION "0.0.1"
#define TAB_STOP 8
#define QUIT_TIMES 2
/*** prototypes ***/

void editor_set_statusmsg(const char* fmt, ...);

enum editor_key {
	BACKSPACE = 127,
	ARROW_LEFT = 1000, // Giving large value to avoid any keypress conflict
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY
};

/*** data ***/

typedef struct erow {
	int size;
	int r_size;
	char *chars;
	char *render;

} erow;
struct editor_config {
	int cx, cy; // Cursor position
	int rx;
	int screenrows;
	int screencols;
	int numrows; // Number of rows in file
	int rowoff; // Row and column offsets for scrolling
	int coloff;
	int dirty;
	erow *rows;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;
};

struct editor_config E;


/*** terminal ***/

void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4); // Clear screen at exit
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
}

void disable_raw_mode() {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
	       die("tcsetattr");
}

void enable_raw_mode() {
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disable_raw_mode);
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | IXON | ICRNL | INPCK | ISTRIP); // Input Field
	raw.c_oflag &= ~(OPOST); // Output Field
	raw.c_cflag &= ~(CS8); // Control Field
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); // Local Field
       /*
	* Disables:
	* 1. ECHO (Echoing)
	* 2. ICANON (Canonical mode)
	* 3. SIGINT (Ctrl+C) /  SIGSTP (Ctrl+Z)
	* 4. XOFF (Ctrl+S) / XON (Ctrl+Q)
	* 5. IEXTEN (Ctrl+V)
	* 6. ICRNL (Ctrl+M: Prevents Ctrl+M being translated from 13 (\r or CR) to 10 (\n or NL)
	* 7. OPOST (Prevents \n to \r\n translation)
	* 8. BRKINT / INPCK / CS8 / ISTRIP (Legacy)
	*/

	raw.c_cc[VMIN] = 0; // Minimum number of bytes of input required before read() can return
	raw.c_cc[VTIME] = 1; // read() waits for n*100ms before return, returns 0 if it times out

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
	// TCSAFLUSH discards unread input before applying changes to terminal
}

int editor_read_key() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
	       if (nread == -1 && errno != EAGAIN)
		       die("read");
		// Cygwin returns -1 with errno EAGAIN instead of 0 when read() times out. Hence, we ignore EAGAIN as an error.
	}
	if (c == '\x1b') {
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
		       	return '\x1b';
		if (seq[0] == '[') {
			if (seq[1] >='0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1':
						case '7':
							return HOME_KEY;
						case '3': return DEL_KEY;
						case '4':
						case '8':
							return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
					}
				}
			}
			else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}


		return '\x1b';
	}

	else
		return c;
}

int get_cursor_position(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) // n command asks the terminal for status information, with an argument of 6 to ask for the cursor position
		return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}

	buf[i] = '\0';
	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	editor_read_key();
	return -1;
}

int get_window_size(int *rows, int *cols) {
	struct winsize ws; // ioctl stores no. of cols and rows in winsize struct ws

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1; // Moves cursor to bottom right of the window. \x1b[999,999H is not used since unlike with B and C, moving the cursor off-screen is undefined behaviour.
		return get_cursor_position(rows, cols);

	}

	else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row ops ***/

int editor_c2r(erow *row, int cx) {
	int rx = 0;
	for (int j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (TAB_STOP - 1) - (rx % TAB_STOP); // Find how many columns left to the next tab stop
		rx++;
	}
	return rx;
}

void editor_update_row(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t')
			tabs++;
	free(row->render);
	row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % TAB_STOP != 0)
				row->render[idx++] = ' ';
		}
		else
			row->render[idx++] = row->chars[j];
	}
	row->render[idx] = '\0';
	row->r_size = idx;
}

void editor_append_row(char *s, size_t len) {
	E.rows = realloc(E.rows, sizeof(erow)*(E.numrows + 1));
	int at = E.numrows;
	E.rows[at].size = len;
	E.rows[at].chars = malloc(len + 1);
	memcpy(E.rows[at].chars, s, len);
	E.rows[at].chars[len] = '\0';

	E.rows[at].r_size = 0;
	E.rows[at].render = NULL;
	editor_update_row(&E.rows[at]);
	E.numrows++;
	E.dirty++;
}

void editor_free_row(erow *row) {
	free(row->render);
	free(row->chars);
}

void editor_del_row(int at) {
	if (at < 0 || at >= E.numrows)
		return;
	editor_free_row(&E.rows[at]);
	memmove(&E.rows[at - 1], &E.rows[at], sizeof(erow)*(E.numrows - at - 1));
	E.numrows--;
	E.dirty++;

}
void editor_row_insert_char(erow *row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editor_update_row(row);
	E.dirty++;
}

void editor_row_append_str(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editor_update_row(row);
	E.dirty++;
}

void editor_row_del_char(erow *row, int at) {
	if (at < 0 || at >= row->size)
		return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editor_update_row(row);
	E.dirty++;

}

/*** editor ops ***/

void editor_insert_char(int c) {
	if (E.cy == E.numrows)
		editor_append_row(" ", 0);
	editor_row_insert_char(&E.rows[E.cy], E.cx, c);
	E.cx++;
}

void editor_del_char() {
	if (E.cy == E.numrows)
		return;
	if (E.cx == 0 && E.cy == 0)
		return;
	erow *row = &E.rows[E.cy];
	if (E.cx > 0) {
		editor_row_del_char(row, E.cx - 1);
		E.cx--;
	}
	else {
		E.cx = E.rows[E.cy - 1].size;
		editor_row_append_str(&E.rows[E.cy - 1], row->chars, row->size);
		editor_del_row(E.cy);
		E.cy--;
	}
}
/*** file i/o ***/

char* editor_rows_to_string(int *buflen) {
	int totlen = 0;
	int j = 0;
	for (j = 0; j < E.numrows; j++)
		totlen += E.rows[j].size + 1;
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.rows[j].chars, E.rows[j].size);
		p += E.rows[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}


void editor_open(char *filename) {
	E.filename = filename;
	FILE *fp = fopen(filename, "r");
	if (!fp)
		die("fopen");
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editor_append_row(line, linelen);
	}

	free(line);
	fclose(fp);
	E.dirty = 0;

}

void editor_save() {
	if (E.filename == NULL)
		return;
	int len;
	char *buf = editor_rows_to_string(&len);
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644); // O_CREAT -> Creates file if it doesn't exist, O_RDWR -> Opens file for reading and writing, 0644 are permissions allowing r/w for owner and r for other users. O_TRUNC could be used, but in case of write fail, the entire file is lost. Hence, we manually truncate the file.

	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {// Sets file size to specified length len, truncates extra data.
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editor_set_statusmsg("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editor_set_statusmsg("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void ab_append(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL)
		return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void ab_free(struct abuf *ab) {
	free(ab -> b);
}

/*** output ***/

void editor_scroll() {
	E.rx = 0;
	if (E.cy < E.numrows)
		E.rx = editor_c2r(&E.rows[E.cy], E.cx);
	if (E.cy < E.rowoff)
		E.rowoff = E.cy;
	if (E.cy >= E.rowoff + E.screenrows)
		E.rowoff = E.cy - E.screenrows + 1;
	if (E.cx < E.coloff)
		E.coloff = E.rx;
	if (E.cx >= E.coloff + E.screencols)
		E.coloff = E.rx - E.screenrows + 1;
}

void editor_draw_rows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo-clone editor -- version %s", KC_VERSION);
				if (welcomelen > E.screencols)
					welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					ab_append(ab, "~", 1);
					padding--;
				}
				while(padding--)
					ab_append(ab, " ", 1);
				ab_append(ab, welcome, welcomelen);
			}
			else
				ab_append(ab, "~", 1);
		}
		else {
			int len = E.rows[filerow].r_size - E.coloff;
			if (len < 0)
				len = 0;
			if (len > E.screencols)
				len = E.screencols;
			ab_append(ab, &E.rows[filerow].render[E.coloff], len);
		}

		ab_append(ab, "\x1b[K", 3); // Erase in line
		ab_append(ab, "\r\n", 2);
	}
}

void editor_draw_statusbar(struct abuf *ab) {
	ab_append(ab, "\x1b[7m" , 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s%s - %d lines", E.filename ? E.filename : "[No Name]", E.dirty ? "*": "", E.numrows);
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

	if (len > E.screencols)
		len = E.screencols;
	ab_append(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			ab_append(ab, rstatus, rlen);
			break;
		}
		else {
			ab_append(ab, " " , 1);
			len++;
		}
	}

	ab_append(ab, "\x1b[m", 3);
	ab_append(ab, "\r\n" , 2);
}

void editor_draw_messagebar(struct abuf *ab) {
	ab_append(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		ab_append(ab, E.statusmsg, msglen);
}

void editor_refresh_screen() {
	editor_scroll();

	struct abuf ab = ABUF_INIT;
	ab_append(&ab, "\x1b[?25l", 6); // Hides cursor

	/*
	 * Previously:
	 * ab_append(&ab, "\x1b[2J", 4);
	 * \x1b[ is an escape sequence. Escape sequences instruct the terminal to perform text formatting.
	 * The J command clears the screen. The argument passed is 2, which clears the entire screen.
	 * <esc>[1J clears screen up to the cursor, <esc>[0J or <esc>[J (default) clears screen from cursor to end.
	 * It is more optimal to clear each line as we redraw them.
	 */

	ab_append(&ab, "\x1b[H", 3); // H command positions the cursor taking a (row) and b (column) as arguments, or <esc>[a;bH. <esc>[H or <esc[1;1H positions the cursor at the first row and first column.
	editor_draw_rows(&ab);
	editor_draw_statusbar(&ab);
	editor_draw_messagebar(&ab);
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); // Moves cursor to position
	ab_append(&ab, buf, strlen(buf));

	ab_append(&ab, "\x1b[?25h", 6); // Shows Cursor
	write(STDOUT_FILENO, ab.b, ab.len); // Updates entire screen at once, remove flickering
	ab_free(&ab);
}

void editor_set_statusmsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/*** input ***/

void editor_move_cursor(int key) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.rows[E.cy];
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0)
				E.cx--;
			else {
				E.cy--;
				E.cx = E.rows[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size)
				E.cx++;
			else {
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0)
				E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows)
				E.cy++;
			break;
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = E.screencols - 1;
			break;
	}
	row = (E.cy >= E.numrows) ? NULL : &E.rows[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen)
		E.cx = rowlen;
}

void editor_process_keypress() {
	static int quit_times = QUIT_TIMES;
	int c = editor_read_key();
	switch (c) {
		case '\r':
			/* todo */
			break;
		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editor_set_statusmsg("Warning, file has unsaved changes. Press ^Q %d more times to save", quit_times);
				quit_times--;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4); // Clear screen at exit
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		case CTRL_KEY('s'):
			editor_save();
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP)
					E.cy = E.rowoff;
				else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.screenrows - 1;
					if (E.cy > E.numrows)
						E.cy = E.numrows;
				}
				int times = E.screenrows;
				while (times--)
					editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (c == DEL_KEY)
				editor_move_cursor(ARROW_RIGHT);
			editor_del_char();

			break;
		case HOME_KEY:
		case END_KEY:
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editor_move_cursor(c);
			break;
		case CTRL_KEY('l'):
		case '\x1b':
			break;
		default:
			editor_insert_char(c);
			break;
	}

	quit_times = QUIT_TIMES;
}

/*** init ***/

void init_editor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.numrows = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.rows = NULL;
	E.filename = NULL;
	E.dirty = 0;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	if (get_window_size(&E.screenrows, &E.screencols) == -1)
		die("get_window_size");
	E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
	enable_raw_mode();
	init_editor();
	if (argc >= 2)
		editor_open(argv[1]);

	editor_set_statusmsg("^s to save, ^q to quit");

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}
	return 0;

}

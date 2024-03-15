/*** includes ***/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editor_config {
	int screenrows;
	int screencols;
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

char editor_read_key() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
	       if (nread == -1 && errno != EAGAIN)
		       die("read");
		// Cygwin returns -1 with errno EAGAIN instead of 0 when read() times out. Hence, we ignore EAGAIN as an error.
	}
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

void editor_draw_rows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		ab_append(ab, "~", 1);
		ab_append(ab, "\x1b[K", 3); // Erase in line
		if (y < E.screenrows - 1) 
			ab_append(ab, "\r\n", 2);
	}
}

void editor_refresh_screen() {
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
	ab_append(&ab, "\x1b[H", 3);
	ab_append(&ab, "\x1b[?25l", 6); // Shows Cursor
	write(STDOUT_FILENO, ab.b, ab.len); // Updates entire screen at once, remove flickering
	ab_free(&ab);
}

/*** input ***/

void editor_process_keypress() {
	char c = editor_read_key();
	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4); // Clear screen at exit
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
	}	
}

/*** init ***/

void init_editor() {
	if (get_window_size(&E.screenrows, &E.screencols) == -1)
		die("get_window_size");
}

int main() {
	enable_raw_mode();
	init_editor();
	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}
	return 0;

}

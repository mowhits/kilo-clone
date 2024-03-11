/*** includes ***/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editor_config {
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


/*** output ***/

void editor_draw_rows() {
	int y;
	for (y = 0; y < 24; y++) {
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

void editor_refresh_screen() {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	/*
	* \x1b[ is an escape sequence. Escape sequences instruct the terminal to perform text formatting. 
	* The J command clears the screen. The argument passed is 2, which clears the entire screen. 
	* <esc>[1J clears screen up to the cursor, <esc>[0J or <esc>[J (default) clears screen from cursor to end.
	*/
	write(STDOUT_FILENO, "\x1b[H", 3); // H command positions the cursor taking a (row) and b (column) as arguments, or <esc>[a;bH. <esc>[H or <esc[1;1H positions the cursor at the first row and first column.
	editor_draw_rows();
	write(STDOUT_FILENO, "\x1b[H", 3);
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

int main() {
	enable_raw_mode();

	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}
	return 0;
}

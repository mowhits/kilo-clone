/*** includes ***/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s){
	perror(s);
	exit(1);
}

void disable_raw_mode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
	       die("tcsetattr");	
}

void enable_raw_mode(){
	if(tcgetattr(STDIN_FILENO, &orig_termios) == -1)
		die("tcgetattr");
	atexit(disable_raw_mode);
	struct termios raw = orig_termios;
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

/*** init ***/

int main(){
	enable_raw_mode();
	while (1) {
		char c = '\0'; // See line 33
		if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
			die("read");
		// Cygwin returns -1 with errno EAGAIN instead of 0 when read() times out. Hence, we ignore EAGAIN as an error.
		if (iscntrl(c))
			printf("%d\r\n", c);
		else
			printf("%d ('%c')\r\n", c, c);
		if (c == 'q')
			break;
	}
	return 0;
}

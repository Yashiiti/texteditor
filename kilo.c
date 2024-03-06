/* includes */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
enum editorKey {
  ARROW_LEFT = 10000,
  ARROW_RIGHT ,
  ARROW_UP ,
  ARROW_DOWN ,
  HOME_KEY,
  DEL_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};
/*  data    */
typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  int numrows;
  int coloff;
  int rowoff;
  erow *row;
  struct termios orig_termios;
};

struct editorConfig E;

/* terminal */

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
  write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor

  perror(s); // print error message
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios)==-1) die("tcsetattr"); // set terminal attributes back to original
}
void enableRawMode() {
   if ( tcgetattr(STDIN_FILENO, &E.orig_termios)==-1) die("tcgetattr"); // get terminal attributes
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;
    // modify terminal attributes
    // IXON - disable ctrl-s and ctrl-q
    // ICRNL - disable ctrl-m
   
    // BRKINT - disable break condition
    // INPCK - disable parity checking
    // ISTRIP - disable stripping 8th bit
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // input flags
     // OPOST - disable output processing
    raw.c_oflag &= ~(OPOST); // output flags
    // CS8 - set character size to 8 bits per byte
    raw.c_cflag |= (CS8); // control flags
    // ECHO - echo input characters
    // ICANON - canonical mode
    // IEXTEN - enable ctrl-v and ctrl-o
    // ISIG - enable ctrl-c and ctrl-z signals
    // we are disabling them here
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // local flags
    raw.c_cc[VMIN] = 0; // minimum bytes before read() can return
    raw.c_cc[VTIME] = 1; // maximum time before read() returns in 1/10th of a second
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)==-1) die("tcsetattr"); // set terminal attributes
}
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY; 
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
          }
        }
      } else {
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
    else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;



  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
  if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
  if (buf[i] == 'R') break;
  i++;
}
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; // read the cursor position into rows and cols
  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
     if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // move cursor to bottom right C for right B for bottom movement
    return getCursorPosition(rows, cols);
  }
   else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
/*** row operations ***/
void editorAppendRow(char *s, size_t len) {

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

/* file i/o */
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}
/* append buffer */
struct abuf {
  char *b;
  int len;
  int capacity;
};
#define ABUF_INIT {NULL, 0,0}
void abAppend(struct abuf *ab, const char *s, int len) {
  
  while (ab->len + len >= ab->capacity) {
        if (ab->capacity == 0) {
            ab->capacity = 1; // Initial capacity
        } else {
            ab->capacity *= 2; // Double the capacity
        }
        ab->b = realloc(ab->b, ab->capacity);
        if (ab->b == NULL) {
            // Handle realloc failure
            return;
        }
    }
    // Copy the new data to the end of the buffer
    memcpy(ab->b + ab->len, s, len);
    ab->len += len;
}
void abFree(struct abuf *ab) {
  free(ab->b);
  ab->b = NULL;
  ab->len = 0;
  ab->capacity = 0;

}

/* output */
void editorScroll() {
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
   if (E.cx < E.coloff) {
    E.coloff = E.cx;
  }
  if (E.cx >= E.coloff + E.screencols) {
    E.coloff = E.cx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      
      int len = E.row[filerow].size - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].chars[E.coloff], len);
      
    }
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  // printf("1");
  editorScroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  
  abAppend(&ab, "\x1b[H", 3); // reposition cursor
  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx-E.coloff) + 1); // cursor bar
 
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);  // show cursor 
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/* input */
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;}
     else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;} 
      break;
    case ARROW_RIGHT:
    if (row && E.cx < row->size) {
      E.cx++;}
    else if (row && E.cx == row->size) {
      E.cy++;
        E.cx = 0;
    }
      break;
    case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;}
      break;
    case ARROW_DOWN:
    if (E.cy < E.numrows) {
      E.cy++;}
      break;
  }
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
      write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor
      exit(0);
      break;
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      E.cx = E.screencols - 1;
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}

/* init */

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numrows = 0;
  E.rowoff=0;
  E.coloff=0;
  E.row = NULL; 
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if (argc>=2){
    editorOpen(argv[1]);
    }
     while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
/* PDCurses */

#include <curspriv.h>

/*man-start**************************************************************

getch
-----

### Synopsis

    int getch(void);
    int wgetch(WINDOW *win);
    int mvgetch(int y, int x);
    int mvwgetch(WINDOW *win, int y, int x);
    int ungetch(int ch);
    int flushinp(void);

    int get_wch(wint_t *wch);
    int wget_wch(WINDOW *win, wint_t *wch);
    int mvget_wch(int y, int x, wint_t *wch);
    int mvwget_wch(WINDOW *win, int y, int x, wint_t *wch);
    int unget_wch(const wchar_t wch);

    unsigned long PDC_get_key_modifiers(void);
    int PDC_return_key_modifiers(bool flag);

### Description

   With the getch(), wgetch(), mvgetch(), and mvwgetch() functions, a
   character is read from the terminal associated with the window. In
   nodelay mode, if there is no input waiting, the value ERR is
   returned. In delay mode, the program will hang until the system
   passes text through to the program. Depending on the setting of
   cbreak(), this will be after one character or after the first
   newline. Unless noecho() has been set, the character will also be
   echoed into the designated window.

   If keypad() is TRUE, and a function key is pressed, the token for
   that function key will be returned instead of the raw characters.
   Possible function keys are defined in <curses.h> with integers
   beginning with 0401, whose names begin with KEY_.

   If nodelay(win, TRUE) has been called on the window and no input is
   waiting, the value ERR is returned.

   ungetch() places ch back onto the input queue to be returned by the
   next call to wgetch().

   flushinp() throws away any type-ahead that has been typed by the user
   and has not yet been read by the program.

   wget_wch() is the wide-character version of wgetch(), available when
   PDCurses is built with the PDC_WIDE option. It takes a pointer to a
   wint_t rather than returning the key as an int, and instead returns
   KEY_CODE_YES if the key is a function key. Otherwise, it returns OK
   or ERR. It's important to check for KEY_CODE_YES, since regular wide
   characters can have the same values as function key codes.

   unget_wch() puts a wide character on the input queue.

   PDC_get_key_modifiers() returns the keyboard modifiers (shift,
   control, alt, numlock) effective at the time of the last getch()
   call. Use the macros PDC_KEY_MODIFIER_* to determine which
   modifier(s) were set. PDC_return_key_modifiers() tells getch() to
   return modifier keys pressed alone as keystrokes (KEY_ALT_L, etc.).
   These may not work on all platforms.

   NOTE: getch() and ungetch() are implemented as macros, to avoid
   conflict with many DOS compiler's runtime libraries.

### Return Value

   These functions return ERR or the value of the character, meta
   character or function key token.

### Portability
                             X/Open  ncurses  NetBSD
    getch                       Y       Y       Y
    wgetch                      Y       Y       Y
    mvgetch                     Y       Y       Y
    mvwgetch                    Y       Y       Y
    ungetch                     Y       Y       Y
    flushinp                    Y       Y       Y
    get_wch                     Y       Y       Y
    wget_wch                    Y       Y       Y
    mvget_wch                   Y       Y       Y
    mvwget_wch                  Y       Y       Y
    unget_wch                   Y       Y       Y
    PDC_get_key_modifiers       -       -       -

**man-end****************************************************************/

#define _INBUFSIZ   512 /* size of terminal input buffer */
#define NUNGETCH    256 /* max # chars to ungetch() */

static int c_pindex = 0;    /* putter index */
static int c_gindex = 1;    /* getter index */
static int c_ungind = 0;    /* ungetch() push index */
static int c_ungch[NUNGETCH];   /* array of ungotten chars */

static int _mouse_key(void)
{
    int i, key = KEY_MOUSE;
    const mmask_t mbe = SP->_trap_mbe;

    /* Filter unwanted mouse events */

    for (i = 0; i < 3; i++)
    {
        if (SP->mouse_status.changes & (1 << i))
        {
            int shf = i * 5;
            short button = SP->mouse_status.button[i] & BUTTON_ACTION_MASK;

            if (   (!(mbe & (BUTTON1_PRESSED << shf)) &&
                    (button == BUTTON_PRESSED))

                || (!(mbe & (BUTTON1_CLICKED << shf)) &&
                    (button == BUTTON_CLICKED))

                || (!(mbe & (BUTTON1_DOUBLE_CLICKED << shf)) &&
                    (button == BUTTON_DOUBLE_CLICKED))

                        /* added triple clicks 2011 jun 4: BJG */
                || (!(mbe & (BUTTON1_TRIPLE_CLICKED << shf)) &&
                    (button == BUTTON_TRIPLE_CLICKED))

                || (!(mbe & (BUTTON1_MOVED << shf)) &&
                    (button == BUTTON_MOVED))

                || (!(mbe & (BUTTON1_RELEASED << shf)) &&
                    (button == BUTTON_RELEASED))
            )
                SP->mouse_status.changes ^= (1 << i);
        }
    }

    if (SP->mouse_status.changes & PDC_MOUSE_MOVED)
    {
        if (!(mbe & (BUTTON1_MOVED|BUTTON2_MOVED|BUTTON3_MOVED | REPORT_MOUSE_POSITION)))
            SP->mouse_status.changes ^= PDC_MOUSE_MOVED;
    }

    if (SP->mouse_status.changes &
        (PDC_MOUSE_WHEEL_UP|PDC_MOUSE_WHEEL_DOWN))
    {
        if (!(mbe & MOUSE_WHEEL_SCROLL))
            SP->mouse_status.changes &=
                ~(PDC_MOUSE_WHEEL_UP|PDC_MOUSE_WHEEL_DOWN);
    }

    if (!SP->mouse_status.changes)
        return -1;

    /* Check for click in slk area */

    i = PDC_mouse_in_slk(SP->mouse_status.y, SP->mouse_status.x);

    if (i)
    {
        if (SP->mouse_status.button[0] & (BUTTON_PRESSED|BUTTON_CLICKED))
            key = KEY_F(i);
        else
            key = -1;
    }

    return key;
}

#define WAIT_FOREVER    -1

int wgetch(WINDOW *win)
{
    static int buffer[_INBUFSIZ];   /* character buffer */
    int key, remaining_millisecs;

    PDC_LOG(("wgetch() - called\n"));

    if (!win)
        return ERR;

    if (SP->delaytenths)
        remaining_millisecs = 100 * SP->delaytenths;
    else
        remaining_millisecs = win->_delayms;
    if( !remaining_millisecs && !win->_nodelay)
        remaining_millisecs = WAIT_FOREVER;

    /* refresh window when wgetch is called if there have been changes
       to it and it is not a pad */

    if (!(win->_flags & _PAD) && ((!win->_leaveit &&
         (win->_begx + win->_curx != SP->curscol ||
          win->_begy + win->_cury != SP->cursrow)) || is_wintouched(win)))
        wrefresh(win);

    /* if ungotten char exists, remove and return it */

    if (c_ungind)
        return c_ungch[--c_ungind];

    /* if normal and data in buffer */

    if ((!SP->raw_inp && !SP->cbreak) && (c_gindex < c_pindex))
        return buffer[c_gindex++];

    /* prepare to buffer data */

    c_pindex = 0;
    c_gindex = 0;

    /* to get here, no keys are buffered. go and get one. */

    for (;;)            /* loop for any buffering */
    {

        /* is there a keystroke ready? */

        if (!PDC_check_key())
        {
            /* if not, handle timeout() and halfdelay() */
            int nap_time = 50;

            if (remaining_millisecs != WAIT_FOREVER)
            {
                if (!remaining_millisecs)
                    return ERR;
                if( nap_time > remaining_millisecs)
                    nap_time = remaining_millisecs;
                remaining_millisecs -= nap_time;
            }
            napms( nap_time);
            continue;   /* then check again */
        }

        /* if there is, fetch it */

        key = PDC_get_key();

        if (SP->key_code)
        {
            /* filter special keys if not in keypad mode */

            if (!win->_use_keypad)
                key = -1;

            /* filter mouse events; translate mouse clicks in the slk
               area to function keys */

            else if (key == KEY_MOUSE)
                key = _mouse_key();
        }

        /* unwanted key? loop back */

        if (key == -1)
            continue;

        /* translate CR */

        if (key == '\r' && SP->autocr && !SP->raw_inp)
            key = '\n';

        /* if echo is enabled */

        if (SP->echo && !SP->key_code)
        {
            waddch(win, key);
            wrefresh(win);
        }

        /* if no buffering */

        if (SP->raw_inp || SP->cbreak)
            return key;

        /* if no overflow, put data in buffer */

        if (key == '\b')
        {
            if (c_pindex > c_gindex)
                c_pindex--;
        }
        else
            if (c_pindex < _INBUFSIZ - 2)
                buffer[c_pindex++] = key;

        /* if we got a line */

        if (key == '\n' || key == '\r')
            return buffer[c_gindex++];
    }
}

int mvgetch(int y, int x)
{
    PDC_LOG(("mvgetch() - called\n"));

    if (move(y, x) == ERR)
        return ERR;

    return wgetch(stdscr);
}

int mvwgetch(WINDOW *win, int y, int x)
{
    PDC_LOG(("mvwgetch() - called\n"));

    if (wmove(win, y, x) == ERR)
        return ERR;

    return wgetch(win);
}

int PDC_ungetch(int ch)
{
    PDC_LOG(("ungetch() - called\n"));

    if (c_ungind >= NUNGETCH)   /* pushback stack full */
        return ERR;

    c_ungch[c_ungind++] = ch;

    return OK;
}

int flushinp(void)
{
    PDC_LOG(("flushinp() - called\n"));

    PDC_flushinp();

    c_gindex = 1;           /* set indices to kill buffer */
    c_pindex = 0;
    c_ungind = 0;           /* clear c_ungch array */

    return OK;
}

unsigned long PDC_get_key_modifiers(void)
{
    PDC_LOG(("PDC_get_key_modifiers() - called\n"));

    return SP->key_modifiers;
}

int PDC_return_key_modifiers(bool flag)
{
    PDC_LOG(("PDC_return_key_modifiers() - called\n"));

    SP->return_key_modifiers = flag;
    return PDC_modifiers_set();
}

#ifdef PDC_WIDE
int wget_wch(WINDOW *win, wint_t *wch)
{
    int key;

    PDC_LOG(("wget_wch() - called\n"));

    if (!wch)
        return ERR;

    key = wgetch(win);

    if (key == ERR)
        return ERR;

    *wch = key;

    return SP->key_code ? KEY_CODE_YES : OK;
}

int get_wch(wint_t *wch)
{
    PDC_LOG(("get_wch() - called\n"));

    return wget_wch(stdscr, wch);
}

int mvget_wch(int y, int x, wint_t *wch)
{
    PDC_LOG(("mvget_wch() - called\n"));

    if (move(y, x) == ERR)
        return ERR;

    return wget_wch(stdscr, wch);
}

int mvwget_wch(WINDOW *win, int y, int x, wint_t *wch)
{
    PDC_LOG(("mvwget_wch() - called\n"));

    if (wmove(win, y, x) == ERR)
        return ERR;

    return wget_wch(win, wch);
}

int unget_wch(const wchar_t wch)
{
    return PDC_ungetch(wch);
}
#endif

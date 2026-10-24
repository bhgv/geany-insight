/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-serial.h"
#include "insight-terminal.h"
#include "insight-help.h"
#include "main.h"
#include "ui_utils.h"
#include <string.h>
#include <errno.h>
#ifdef G_OS_WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#endif
typedef struct {
 GtkWidget *page,*view;GByteArray *pending;guint timer,number;gboolean echo;
#ifdef G_OS_WIN32
 HANDLE handle;OVERLAPPED read_op,write_op;gboolean reading,writing;guint write_size;gchar read_buffer[8192],write_buffer[8192];
#else
 gint fd;struct termios original;gboolean restore;
#endif
} SerialSession;
static struct { GtkWidget *page,*tabs,*connection,*ports,*baud,*bits,*stop,*parity,*echo,*status;GPtrArray *sessions;SerialSession *selected;gboolean closing; } serial;
static void message(SerialSession *s,const gchar *text) {
 if(serial.closing) return;gtk_label_set_text(GTK_LABEL(serial.status),text);if(s) { gchar *line=g_strdup_printf("\r\n[TTY] %s\r\n",text);insight_terminal_feed(s->view,line,strlen(line));g_free(line); }
}
static void close_session(SerialSession *s) {
 if(s->timer) { g_source_remove(s->timer);s->timer=0; }
#ifdef G_OS_WIN32
 if(s->handle!=INVALID_HANDLE_VALUE) { DWORD done;CancelIo(s->handle);if(s->reading) GetOverlappedResult(s->handle,&s->read_op,&done,TRUE);if(s->writing) GetOverlappedResult(s->handle,&s->write_op,&done,TRUE);CloseHandle(s->handle);s->handle=INVALID_HANDLE_VALUE; }
 s->reading=s->writing=FALSE;if(s->read_op.hEvent) { CloseHandle(s->read_op.hEvent);s->read_op.hEvent=NULL; }if(s->write_op.hEvent) { CloseHandle(s->write_op.hEvent);s->write_op.hEvent=NULL; }
#else
 if(s->fd>=0) { if(s->restore) tcsetattr(s->fd,TCSANOW,&s->original);close(s->fd);s->fd=-1; }
#endif
 g_byte_array_set_size(s->pending,0);
}
static gboolean connected(SerialSession *s) {
#ifdef G_OS_WIN32
 return s->handle!=INVALID_HANDLE_VALUE;
#else
 return s->fd>=0;
#endif
}
static void write_terminal(const gchar *bytes,gsize length,gpointer data) {
 SerialSession *s=data;if(!connected(s) || serial.closing) return;
 if(length>1024*1024 || s->pending->len+length>1024*1024) { message(s,"TTY output queue is full");return; }
 g_byte_array_append(s->pending,(const guint8 *)bytes,length);if(s->echo) insight_terminal_feed(s->view,bytes,length);
}
static gboolean tick(gpointer data) {
 SerialSession *s=data;gchar buffer[8192];gboolean ok=TRUE;
#ifdef G_OS_WIN32
 DWORD errors=0,count=0;COMSTAT stat;
 if(!ClearCommError(s->handle,&errors,&stat)) ok=FALSE;
 if(ok && s->reading) { if(GetOverlappedResult(s->handle,&s->read_op,&count,FALSE)) { s->reading=FALSE;if(count) insight_terminal_feed(s->view,s->read_buffer,count); }else if(GetLastError()!=ERROR_IO_INCOMPLETE) ok=FALSE; }
 if(ok && !s->reading && stat.cbInQue) { ResetEvent(s->read_op.hEvent);if(ReadFile(s->handle,s->read_buffer,MIN((DWORD)sizeof(s->read_buffer),stat.cbInQue),&count,&s->read_op)) { if(count) insight_terminal_feed(s->view,s->read_buffer,count); }else if(GetLastError()==ERROR_IO_PENDING) s->reading=TRUE;else ok=FALSE; }
 if(ok && s->writing) { if(GetOverlappedResult(s->handle,&s->write_op,&count,FALSE)) { s->writing=FALSE;if(count) g_byte_array_remove_range(s->pending,0,MIN(count,s->pending->len)); }else if(GetLastError()!=ERROR_IO_INCOMPLETE) ok=FALSE; }
 if(ok && !s->writing && s->pending->len) { s->write_size=MIN(s->pending->len,8192u);memcpy(s->write_buffer,s->pending->data,s->write_size);ResetEvent(s->write_op.hEvent);if(WriteFile(s->handle,s->write_buffer,s->write_size,&count,&s->write_op)) { if(count) g_byte_array_remove_range(s->pending,0,MIN(count,s->pending->len)); }else if(GetLastError()==ERROR_IO_PENDING) s->writing=TRUE;else ok=FALSE; }
#else
 struct pollfd descriptor={s->fd,POLLIN,0};if(poll(&descriptor,1,0)<0 && errno!=EINTR) ok=FALSE;if(descriptor.revents&(POLLERR|POLLHUP|POLLNVAL)) ok=FALSE;
 for(guint n=0;ok && n<8;n++) { ssize_t count=read(s->fd,buffer,sizeof(buffer));if(count>0) insight_terminal_feed(s->view,buffer,count);else { if(count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) ok=FALSE;break; } }
 if(ok && s->pending->len) { ssize_t count=write(s->fd,s->pending->data,MIN(s->pending->len,8192u));if(count>0) g_byte_array_remove_range(s->pending,0,count);else if(count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) ok=FALSE; }
#endif
 if(!ok) { s->timer=0;close_session(s);message(s,"TTY device disconnected or I/O failed; connection closed");return G_SOURCE_REMOVE; }return G_SOURCE_CONTINUE;
}
static gint choice(GtkWidget *combo) { gchar *text=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));gint value=text ? (gint)g_ascii_strtoll(text,NULL,10):0;g_free(text);return value; }
#ifndef G_OS_WIN32
static speed_t speed(gint baud) {
 switch(baud) { case 300:return B300;case 600:return B600;case 1200:return B1200;case 2400:return B2400;case 4800:return B4800;case 9600:return B9600;case 19200:return B19200;case 38400:return B38400;
#ifdef B57600
 case 57600:return B57600;
#endif
#ifdef B115200
 case 115200:return B115200;
#endif
#ifdef B230400
 case 230400:return B230400;
#endif
 default:return 0; }
}
#endif
static void connect_port(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;SerialSession *s=serial.selected;if(!s) return;close_session(s);
 const gchar *port=gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(serial.ports))));gint baud=choice(serial.baud),bits=choice(serial.bits),stops=choice(serial.stop),parity=gtk_combo_box_get_active(GTK_COMBO_BOX(serial.parity));
 if(!*port || strlen(port)>1024 || strpbrk(port,"\r\n\033") || bits<5 || bits>8 || (stops!=1 && stops!=2)) { message(s,"Choose a serial port and valid connection parameters");return; }
#ifdef G_OS_WIN32
 if(!g_str_has_prefix(port,"COM") || !g_ascii_isdigit(port[3])) { message(s,"Choose a COM port");return; }
 gchar *device=g_strconcat("\\\\.\\",port,NULL);gunichar2 *native=g_utf8_to_utf16(device,-1,NULL,NULL,NULL);g_free(device);s->handle=CreateFileW((LPCWSTR)native,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);g_free(native);
 if(s->handle==INVALID_HANDLE_VALUE) { message(s,"Cannot open COM port; check its name, permissions and whether another terminal uses it");return; }
 DCB config={0};config.DCBlength=sizeof(config);GetCommState(s->handle,&config);config.BaudRate=baud;config.ByteSize=bits;config.Parity=parity==1 ? EVENPARITY:parity==2 ? ODDPARITY:NOPARITY;config.StopBits=stops==2 ? TWOSTOPBITS:ONESTOPBIT;config.fBinary=TRUE;config.fParity=parity!=0;config.fOutxCtsFlow=FALSE;config.fOutxDsrFlow=FALSE;config.fDtrControl=DTR_CONTROL_DISABLE;config.fRtsControl=RTS_CONTROL_DISABLE;config.fOutX=FALSE;config.fInX=FALSE;
 COMMTIMEOUTS timeouts={MAXDWORD,0,0,0,0};if(!SetCommState(s->handle,&config) || !SetCommTimeouts(s->handle,&timeouts)) { close_session(s);message(s,"The COM driver rejected these serial parameters");return; }
#else
 s->restore=FALSE;speed_t rate=speed(baud);if(!rate || *port!='/') { message(s,"Choose an absolute device path and a supported baud rate");return; }
 s->fd=open(port,O_RDWR|O_NOCTTY|O_NONBLOCK);if(s->fd<0) { gchar *error=g_strdup_printf("Cannot open TTY: %s",g_strerror(errno));message(s,error);g_free(error);return; }fcntl(s->fd,F_SETFD,FD_CLOEXEC);
 struct termios config;if(tcgetattr(s->fd,&config)<0) { close_session(s);message(s,"This path is not an accessible serial/TTY device");return; }s->original=config;s->restore=TRUE;
 config.c_iflag=0;config.c_oflag=0;config.c_lflag=0;config.c_cflag=CLOCAL|CREAD|(bits==5 ? CS5:bits==6 ? CS6:bits==7 ? CS7:CS8);if(stops==2) config.c_cflag|=CSTOPB;if(parity) config.c_cflag|=PARENB;if(parity==2) config.c_cflag|=PARODD;config.c_cc[VMIN]=0;config.c_cc[VTIME]=0;cfsetispeed(&config,rate);cfsetospeed(&config,rate);
 if(tcsetattr(s->fd,TCSANOW,&config)<0) { close_session(s);message(s,"The TTY driver rejected these serial parameters");return; }
#endif
#ifdef G_OS_WIN32
 s->read_op.hEvent=CreateEvent(NULL,TRUE,FALSE,NULL);s->write_op.hEvent=CreateEvent(NULL,TRUE,FALSE,NULL);if(!s->read_op.hEvent || !s->write_op.hEvent) { close_session(s);message(s,"Cannot allocate serial I/O events");return; }
#endif
 s->echo=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(serial.echo));s->timer=g_timeout_add(20,tick,s);gchar *text=g_strdup_printf("Connected: %s • %d baud • %d data bits • %d stop bits",port,baud,bits,stops);message(s,text);g_free(text);gtk_notebook_set_current_page(GTK_NOTEBOOK(serial.tabs),gtk_notebook_page_num(GTK_NOTEBOOK(serial.tabs),s->page));gtk_widget_grab_focus(s->view);
}
static void disconnect_port(GtkWidget *widget,gpointer data) { (void)widget;(void)data;if(serial.selected) { close_session(serial.selected);message(serial.selected,"TTY disconnected"); } }
static gint compare_names(gconstpointer a,gconstpointer b) { return g_strcmp0(*(gchar *const *)a,*(gchar *const *)b); }
static void refresh_ports(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;GtkWidget *entry=gtk_bin_get_child(GTK_BIN(serial.ports));gchar *selected=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));GPtrArray *ports=g_ptr_array_new_with_free_func(g_free);
#ifdef G_OS_WIN32
 for(guint n=1;n<=256;n++) { wchar_t name[16],target[4096];swprintf(name,16,L"COM%u",n);if(QueryDosDeviceW(name,target,G_N_ELEMENTS(target))) g_ptr_array_add(ports,g_strdup_printf("COM%u",n)); }
#else
 GDir *dir=g_dir_open("/dev",0,NULL);if(dir) { const gchar *name;while((name=g_dir_read_name(dir)) && ports->len<256) if(g_str_has_prefix(name,"ttyS") || g_str_has_prefix(name,"ttyUSB") || g_str_has_prefix(name,"ttyACM") || g_str_has_prefix(name,"rfcomm") || g_str_has_prefix(name,"cu.") || g_str_has_prefix(name,"tty.")) g_ptr_array_add(ports,g_build_filename("/dev",name,NULL));g_dir_close(dir); }
#endif
 g_ptr_array_sort(ports,compare_names);gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(serial.ports));for(guint n=0;n<ports->len;n++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(serial.ports),g_ptr_array_index(ports,n));if(*selected) gtk_entry_set_text(GTK_ENTRY(entry),selected);else if(ports->len) gtk_combo_box_set_active(GTK_COMBO_BOX(serial.ports),0);g_free(selected);g_ptr_array_unref(ports);
}
static void switched(GtkNotebook *book,GtkWidget *page,guint number,gpointer data) { (void)book;(void)number;(void)data;SerialSession *s=g_object_get_data(G_OBJECT(page),"insight-tty-session");if(s) serial.selected=s;else refresh_ports(NULL,NULL); }
static void new_session(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(serial.sessions->len>=16) { message(NULL,"Up to 16 TTY terminals can be open");return; }SerialSession *s=g_new0(SerialSession,1);s->number=serial.sessions->len+1;s->pending=g_byte_array_new();
#ifdef G_OS_WIN32
 s->handle=INVALID_HANDLE_VALUE;
#else
 s->fd=-1;
#endif
 s->page=gtk_scrolled_window_new(NULL,NULL);s->view=insight_terminal_new(write_terminal,s);gchar *name=g_strdup_printf("insight-tty-terminal-%u",s->number);gtk_widget_set_name(s->view,name);g_free(name);gtk_container_add(GTK_CONTAINER(s->page),s->view);g_object_set_data(G_OBJECT(s->page),"insight-tty-session",s);gchar *hint=g_strdup_printf("TTY terminal %u",s->number);gtk_notebook_append_page(GTK_NOTEBOOK(serial.tabs),s->page,insight_terminal_tab(2,s->number,hint));g_free(hint);g_ptr_array_add(serial.sessions,s);serial.selected=s;gtk_widget_show_all(s->page);gtk_notebook_set_current_page(GTK_NOTEBOOK(serial.tabs),gtk_notebook_page_num(GTK_NOTEBOOK(serial.tabs),s->page));
}
static GtkWidget *options(GtkWidget *grid,gint row,const gchar *label,const gchar *name,const gchar *values,gint active) {
 GtkWidget *combo=gtk_combo_box_text_new(),*title=gtk_label_new(label);gtk_label_set_xalign(GTK_LABEL(title),0);gchar **items=g_strsplit(values,";",-1);for(guint n=0;items[n];n++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),items[n]);g_strfreev(items);gtk_combo_box_set_active(GTK_COMBO_BOX(combo),active);gtk_widget_set_name(combo,name);gtk_grid_attach(GTK_GRID(grid),title,0,row,1,1);gtk_grid_attach(GTK_GRID(grid),combo,1,row,1,1);return combo;
}
void insight_serial_init(void) {
 serial.sessions=g_ptr_array_new();serial.page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_widget_set_name(serial.page,"insight-tty-page");serial.tabs=gtk_notebook_new();gtk_widget_set_name(serial.tabs,"insight-tty-tabs");gtk_notebook_set_tab_pos(GTK_NOTEBOOK(serial.tabs),GTK_POS_RIGHT);gtk_notebook_set_scrollable(GTK_NOTEBOOK(serial.tabs),TRUE);gtk_box_pack_start(GTK_BOX(serial.page),serial.tabs,TRUE,TRUE,0);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);serial.connection=scroll;GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),4);gtk_grid_set_column_spacing(GTK_GRID(grid),6);gtk_container_set_border_width(GTK_CONTAINER(body),6);gtk_container_add(GTK_CONTAINER(scroll),body);gtk_box_pack_start(GTK_BOX(body),grid,FALSE,FALSE,0);gtk_notebook_append_page(GTK_NOTEBOOK(serial.tabs),scroll,insight_terminal_tab(0,0,"TTY / COM connection"));
 serial.ports=gtk_combo_box_text_new_with_entry();gtk_widget_set_name(serial.ports,"insight-tty-port");GtkWidget *title=gtk_label_new("Serial port");gtk_grid_attach(GTK_GRID(grid),title,0,0,1,1);gtk_grid_attach(GTK_GRID(grid),serial.ports,1,0,1,1);gtk_entry_set_width_chars(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(serial.ports))),12);serial.baud=options(grid,1,"Baud rate","insight-tty-baud","300;600;1200;2400;4800;9600;19200;38400;57600;115200;230400",9);serial.bits=options(grid,2,"Data bits","insight-tty-bits","5;6;7;8",3);serial.stop=options(grid,3,"Stop bits","insight-tty-stop","1;2",0);serial.parity=options(grid,4,"Parity","insight-tty-parity","None;Even;Odd",0);
 serial.echo=gtk_check_button_new_with_label("Local echo");gtk_widget_set_name(serial.echo,"insight-tty-echo");gtk_box_pack_start(GTK_BOX(body),serial.echo,FALSE,FALSE,0);GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(body),row,FALSE,FALSE,0);const gchar *labels[]={"Connect","Disconnect","Refresh ports"},*names[]={"insight-tty-connect","insight-tty-disconnect","insight-tty-refresh"};GCallback callbacks[]={G_CALLBACK(connect_port),G_CALLBACK(disconnect_port),G_CALLBACK(refresh_ports)};for(guint n=0;n<3;n++) { GtkWidget *button=gtk_button_new_with_label(labels[n]);gtk_widget_set_name(button,names[n]);gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",callbacks[n],NULL); }
 serial.status=gtk_label_new("TTY / RS-232 — choose a port and connection parameters");gtk_widget_set_name(serial.status,"insight-tty-status");gtk_label_set_selectable(GTK_LABEL(serial.status),TRUE);gtk_label_set_line_wrap(GTK_LABEL(serial.status),TRUE);gtk_label_set_width_chars(GTK_LABEL(serial.status),1);gtk_box_pack_start(GTK_BOX(serial.page),serial.status,FALSE,FALSE,0);GtkWidget *plus=gtk_button_new_from_icon_name("list-add-symbolic",GTK_ICON_SIZE_MENU);gtk_widget_set_name(plus,"insight-tty-new-terminal");gtk_widget_set_tooltip_text(plus,"New TTY terminal");g_signal_connect(plus,"clicked",G_CALLBACK(new_session),NULL);gtk_notebook_set_action_widget(GTK_NOTEBOOK(serial.tabs),plus,GTK_PACK_END);g_signal_connect(serial.tabs,"switch-page",G_CALLBACK(switched),NULL);g_signal_connect(scroll,"map",G_CALLBACK(refresh_ports),NULL);new_session(NULL,NULL);gtk_notebook_set_current_page(GTK_NOTEBOOK(serial.tabs),0);
 insight_panel_help_attach(serial.page,"panel-tty.md");gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),serial.page,gtk_label_new("TTY Terminal"));gtk_widget_show_all(serial.page);
}
void insight_serial_finalize(void) { serial.closing=TRUE;if(!serial.sessions) return;for(guint n=0;n<serial.sessions->len;n++) { SerialSession *s=g_ptr_array_index(serial.sessions,n);close_session(s);gtk_widget_destroy(s->view);g_byte_array_unref(s->pending);g_free(s); }g_ptr_array_unref(serial.sessions);serial.sessions=NULL; }

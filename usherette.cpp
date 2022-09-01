#include <QApplication>
#include <QSettings>

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <utime.h>

#include "pdef.h"
#include "db.h"
#include "cal.h"
#include "flopen.h"
#include "memgive.h"
#include "str.h"
#include "drinfo.h"
#include "smdb.h"
#include "dirscan.h"
#include "log.h"
#include "omdb.h"


#define WIDTH "--width=800 --formatted "
//#define WIDTH ""

#define STD_ERRNULL "2>/dev/null"
//#define STD_ERRNULL ""


static void crash(const char *fmt,...);

class MVDIR;

static void grab(int i)
{
i=i;
}


static char *extension(char *fn)
{
static char nulls[4]={0,0,0,0};
int pos=strlen(fn)-4;
if (pos<0) return(nulls);
return(&fn[pos]);
}

// Simple class to load directory contents into a DYNTBL
class DIRTBL : public DYNAG	 {
public:
DIRTBL	(const char *pth);
private:
};

static int fn_cdx(const char *n)
{
int len=strlen(n);
if (len<9) return(NO);
const char *p=&n[len-7];
if (TOUPPER(p[0])!='C') return(NO);
if (TOUPPER(p[1])!='D') return(NO);
if (!ISDIGIT(p[2])) return(NO);
if (p[3]!='.') return(NO);
return(YES);
}

DIRTBL::DIRTBL(const char *pth):DYNAG(sizeof(FILEINFO))
{
DIRSCAN ds(pth);
FILEINFO fi;
struct dirent *entry;
while ((entry=ds.next(&fi))!=NULLPTR)
	{
	if ((entry->d_type&DT_DIR)!=0) continue;
	strcpy(fi.name,entry->d_name);  // Make it JUST filename - not FULLPATH, as returned by ds.next()
if (!fn_cdx(fi.name))	// Don't store "....cd2 / cd3" files (of ANY type)
   DYNAG::put(&fi);
	}
}

// Create/validate _ttNNNNNN, Rename folder/files, Manage _N.N MovieRating folder
class MVDIR {
public:
MVDIR(char *pth);
int	tt_exists(void) {return(dti_tt!=NOTFND);};
int	valid_tt_file(void);			// Create (user-confirmed) _ttNNNNN file if not already present
int	add_or_update_omdb(void);	// (using values held in EM_KEY 'e' after all other user input)
void 	set_rating(void);
//void	set_e_rating(int rating) {e.e.rating=rating;};
~MVDIR();
char	path[1024];		// Initially, FULLpath passed to constructor - MAY BE CHANGED BY RENAME LATER
private:
int	geti_tt(void);
int	geti_vid(void);
int	geti_srt(void);
int	geti_nfo(void);
void	check_tt_file(char *fn);		// Check _ttNNNNN matches FOLDERname
void	check_base_filename(int ii);	// Check *.mkv, *.srt, *.nfo FILEnames match _ttNNNNN
void	read_nfo(void);
void	rename_file(int dti);
void	rename_folder(void);
void	rarbg_del(void);
char	*tt_filename(void) {return(((FILEINFO*)dt->get(dti_tt))->name);};
DIRTBL   *dt;		// Table of (unpathed) FILENAMES found in the FOLDER passed to constructor
EM_KEY1	e;
char	*foldername;
int	dti_vid, dti_srt, dti_nfo, dti_tt;	// biggest (video), (latest) *.srt, *.nfo, _tt*, or NOTFND
int64_t biggest_vid_sz=0;
};

static void fgets_no_lf(char *buf, int maxct, FILE *f)
{
fgets(buf, maxct-1, f);
int i=strlen(buf);
buf[i-1]=0;
}

static int exec_cmd(const char *cmd, char *buf, int bufsz)
{
FILE *f = popen(cmd,"r");
fgets_no_lf(buf, bufsz, f);
int err=pclose(f);
if (err<0) crash("YAD crashed!");
return(err);
}

// Rename fully-qualified file or folder. Return YES if successful, else NO (fatal error?)
static bool exec_rename(char *from, char *to)
{
char cmd[512];
int statusflag;
strfmt(cmd,"%s %c%s%c %c%s%c", "mv", CHR_QTDOUBLE,from,CHR_QTDOUBLE, CHR_QTDOUBLE,to,CHR_QTDOUBLE);
//printf("%s\r\n",cmd);     // SHOULD WRITE THIS LINE TO A LOG FILE
int id = fork();
if (id == 0)  // then it's the child process
    execlp("/bin/sh","/bin/sh", "-c", cmd, (char *)NULL);
wait(&statusflag);  // wait(&status) until child returns, else it might not be ready to use results
bool ok=(statusflag==0);
if (!ok) crash("Error renaming %s to %s",from,to);
return(ok);
}

// Return an ALLOCATED null-terminated string (length 'len' + EOS byte)
// If passed 'str' was already allocated AND contains a string of at least 'len', just use it
// If 'str' is NULL or contains a SHORTER string, reallocate/resize before copying 'ptr' text
static char *copyhack(char *str, char *ptr, int len)
{
if (len<1) len=0;
if (str==NULLPTR || strlen(str)<len) str=(char*)memrealloc(str,len+1);
if (len) memmove(str,ptr,len);
str[len]=0;
return(str);
}

// Return ALLOCATED pointer to string containing the text value of 'id' field within  'buf'
// If 'buf' doesn't contain 'id', return an EMPTY string (ie. - just a null-byte, being the EOS)
static char *strquote(char *buf, const char *id)
{
static char sep[3]={34,58,34};      // ":" separates api returned id (name of fld) from "contents"
static char *a[4];
static int n=NOTFND; n=(n+1)&3;     // cycles 0,1,2,3,0,1,2,3,0,...
if (buf==NULLPTR) {for (n=3;n>=0;n--) Scrap(a[n]); return(0);}  // "cleanup" call with nullptr
int p=0, q, idlen=strlen(id), buflen=strlen(buf), vallen=NOTFND;
while ((q=stridxc(CHR_QTDOUBLE,&buf[p]))!=NOTFND)
    {
    if (p+q+idlen+5 > buflen) break; // insufficient remaining text to contain sought key+value pair
    if (memcmp(&buf[p+=(q+1)],id,idlen) || !SAME3BYTES(&buf[p+idlen],sep)) continue; // keep looking
    vallen=stridxc(CHR_QTDOUBLE,&buf[p+=(idlen+3)]); // (shouldn't be NOTFND, but ret="" s/b okay)
    break;
    }
return(a[n]=copyhack(a[n],&buf[p],vallen));
}

static char *fix_colon(char *nam)	// Change any ":" in MovieName to " -"
{
char *p;
while ((p=strchr(nam,':'))!=NULL) strins(strdel(p,1)," -");
return(nam);		// Return passed string address as a convenience
}

static void retrieve_api_title(EM_KEY1 *e, char *buf)
{
strncpy(e->e.nam,fix_colon(strquote(buf,"Title")),sizeof(e->e.nam));
}


//#define CURLE ""
#define CURLE "2>/dev/null"

// Call API with ImdbNo to get title + year
static int api_name_from_number(EM_KEY1 *e)
{
char cmd[256], buf[4096]; // Allow PLENTY of space for the ibmdb API call
strfmt(cmd,"%s%s&i=tt%07d' %s", "curl 'http://www.omdbapi.com/?apikey=", APIKEY, e->e.imdb_num, CURLE);
int err=exec_cmd(cmd, buf, sizeof(buf));
retrieve_api_title(e,buf);
e->e.year=a2i(strquote(buf,"Year"),4);
return(*e->e.nam!=0 && e->e.year>1920);    // ok - imdb number is valid - EM_KEY name+year filled in
}

static int32_t tt_number_from_str(const char *s)
{
if (*s=='_') s++;
if (SAME2BYTES(s,"tt")) s+=2;
return(a2l(s,0));
}


static void api_name_from_number_s(EM_KEY1 *e, const char *s)
{
e->e.imdb_num=tt_number_from_str(s);
if (e->e.imdb_num<10000 || !api_name_from_number(e))
	crash("Invalid IMDB Number %d entered",e->e.imdb_num);
}

void MVDIR::read_nfo(void)
{
char *fn = ((FILEINFO*)dt->get(dti_nfo))->name;
char s[512];
int i, len, num=0;
HDL f=flopen(strfmt(s,"%s/%s",path,fn),"r");
while (num==0 && (len=flgetln(s,sizeof(s)-1,f))>=0)   // Look for   <uniqueid type="imdb">2788716</uniqueid>
    {
    if ((i=stridxs("<uniqueid type=",s))!=NOTFND && s[i+15]==34
    &&  SAME4BYTES(&s[i+16],"imdb") && s[i+20]==34 && s[i+21]=='>')
        num=a2l(&s[i+22],0);
    else if ((i=stridxs("www.imdb.com/title/tt",s))!=NOTFND)
        {
        num=a2l(&s[i+21],0);
        }
    }
flclose(f);
if (num==0) return;
if (dti_tt!=NOTFND)
	{
	if (num==e.e.imdb_num) return;
	crash("ImdbNo in *.nfo (%d) doesn't match _tt%d",num,e.e.imdb_num);
	}
e.e.imdb_num=num;
if (!api_name_from_number(&e)) crash("IMDB API rejects tt%d",num);
}

static int any_non_digits(const char *p)
{
while (ISDIGIT(*p)) p++;
return(*p!=0); // If not pointing to EOS nullbyte, it must be a non-digit
}

// Called if _ttNNNNN exists, to fill in EM_KEY Ino and name+year
void MVDIR::check_tt_file(char *fn)	// 1st line should contain "MovieName (YYYY)""
{
int len;
char s[256];
HDL f;
if ((len=strlen(fn))>=8 && len<=11 && SAME3BYTES(fn,"_tt") && !any_non_digits(&fn[3])
&& (e.e.imdb_num=a2l(&fn[3],0))>1000
&& (f=flopen(strfmt(s,"%s/%s",path,fn),"r"))!=NULL
&& flgetln(s,sizeof(s)-1,f)>=8)
	{
	flclose(f);
	if (!strcmp(s,foldername) && SAME2BYTES(&s[len=strlen(s)-7]," (") && (e.e.year=a2i(&s[len+2],4))>1920)
		{
		strancpy(e.e.nam,s,len+1);
		return;
		}
	}
crash("Error reading %s",fn);
}

// Does _ttNNNNN exist? If so, check folder name matches first line therein
int MVDIR::geti_tt(void)
{
int i, ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (i=0;i<dt->ct;i++)
	if (strlen(fi[i].name)>3 && SAME3BYTES(fi[i].name,"_tt"))
		if (ii==NOTFND) ii=i; else crash("Multiple _ttNNNNN files");
if (ii==NOTFND) return(NOTFND);
return(ii);			// NOTFND if no _ttNNNNN
}

int MVDIR::geti_vid(void)
{
biggest_vid_sz=0;
int ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (int i=0;i<dt->ct;i++)
	if (fi[i].size>biggest_vid_sz && stridxs(extension(fi[i].name), ".mp4.mkv.avi")!=NOTFND)
		biggest_vid_sz=fi[ii=i].size;
if (ii==NOTFND) crash("No video files");
return(ii);
}

int MVDIR::geti_srt(void)
{
int	dttm=0;
int ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (int i=0;i<dt->ct;i++)
	if (SAME4BYTES(extension(fi[i].name),".srt") && fi[i].dttm>dttm)
		dttm=fi[ii=i].dttm;
return(ii);			// Feasibly NOTFND if there's no *.srt
}

int MVDIR::geti_nfo(void)
{
int ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (int i=0;i<dt->ct;i++)
	if (SAME4BYTES(extension(fi[i].name),".nfo"))
		if (ii==NOTFND) ii=i; else crash("Multiple *.nfo files");
return(ii);			// NOTFND if no *.srt
}

void MVDIR::check_base_filename(int ii)	// Check *.mkv, *.srt, *.nfo FILEnames
{														// must match _ttNNNNN (already checked that FOLDERname does)
char *fn = ((FILEINFO*)dt->get(ii))->name;
int len=strlen(fn)-4;	// Ignore " (YYYY)" of FOLDERname and ".ext" of FILEname when checking
if (len!=strlen(foldername)-7 || strncmp(foldername,fn,len))
	crash("Unexpected filename %s\r\nconflicts with %s",fn,tt_filename());
}

static char *fiddle(char *mn)	// fiddle with passed movie name to get round CLI and API quirks
{
strxlt(mn,SPACE,'+');
char *p, ins[5]={TAB,BACKSLASH,TAB,TAB,0};	// Allow for MULTIPLE single QT1 marks, so TEMPORARILY
															// use TABs (that can all be xlt'd to QT1's in one fell swoop)
while ((p=strchr(mn,CHR_QTSINGLE))!=NULL) strins(strdel(p,1),ins);
strxlt(mn,TAB,CHR_QTSINGLE);
char ins3[6]={'%','2','6',0};
while ((p=strchr(mn,AMPERSAND))!=NULL) strins(strdel(p,1),ins3);
return(mn);
}

int moviename_in_foldername(char *fn, EM_KEY1 *e)
{
int i=strlen(fn)-7, j;

if ((j=stridxc('(',fn))!=NOTFND && (e->e.year=a2i(&fn[j+1],4))>1920)
	{strancpy(e->e.nam,fn,j); return(YES);}

if (i<0) return(NO);		// (just in case it's a very short folder name)
for(i=j=0; (i=stridxc('.',&fn[j]))!=NOTFND && i<sizeof(e->e.nam)-5; j+=(i+1))
	{
	if ((e->e.year=a2i(&fn[j+i+1],4))>1930 && e->e.year<2030 && !a2err && (fn[j+i+5]=='.' || !fn[j+i+5]))
		{strancpy(e->e.nam,fn,j+i+1); return(YES);}
	}
return(NO);
}

void MVDIR::rarbg_del(void)	// Delete any files in MovieFolder starting with (case-insensitive) "rarbg"
{
for (int i=0;i<dt->ct;i++)
	{
	FILEINFO *fi=(FILEINFO*)dt->get(i);
	char fn[FNAMSIZ], *n=fi->name;
	if (!strncasecmp("rarbg",n,5))
		{
		if (unlink(strfmt(fn,"%s/%s",path,n)))
			sjhlog("Error deleting %s",fn);
		}
	}
}

// Call API with title+year to get ImdbNo
static int api_number_from_name(char *fn, EM_KEY1 *e)
{
if (moviename_in_foldername(fn,e))
	{
	char cmd[512], buf[4096], *p;
	fiddle(strcpy(buf,e->e.nam));
	strfmt(cmd,"%s%s%s%s","curl 'https://www.omdbapi.com/?apikey=", APIKEY, "&t=\"", buf);
	strendfmt(cmd,"\"&y=%d' %s",e->e.year, STD_ERRNULL);
	int err=exec_cmd(cmd, buf, sizeof(buf));
	if (!err && *(p=strquote(buf,"imdbID"))!=0 && SAME2BYTES(p,"tt") && (e->e.imdb_num=a2l(&p[2],0))!=0)
//		if (!strcmp(e->nam,strquote(buf,"Title")) && a2i(strquote(buf,"Year"),4)==e->year)
		{
		retrieve_api_title(e,buf);
    	return(YES);
		}
	}
return(e->e.imdb_num=0);	// zeroise in case we put (invalid) value in there during above processing
}

static char *fmt_name_year(char *s, EM_KEY1 *e)
{
return(strfmt(s,"%s (%d)",e->e.nam,e->e.year));
}

// Create "definitive" _ttNNNNN containing my preferred MovieName
static void write_tt_file(const char *folder, EM_KEY1 *e)
{
char fn[FNAMSIZ];
HDL f=flopen(strfmt(fn,"%s/_tt%d", folder, e->e.imdb_num),"w");
fmt_name_year(fn,e);
flputln(fn,f);
flclose(f);
}

void MVDIR::rename_file(int dti)	// Rename avi / srt / nfo file in 'dt' to match preferred MovieName
{
char *fn= ((FILEINFO*)dt->get(dti))->name;
int len=strlen(fn)-4;
if (len==strlen(e.e.nam) && !strncmp(e.e.nam,fn,len)) return;	// No need to rename
char from[FNAMSIZ], to[FNAMSIZ];
strfmt(from,"%s/%s",path,fn);
strfmt(to,"%s/%s%s",path,e.e.nam,extension(fn));
exec_rename(from,to);
}

void MVDIR::rename_folder(void)	// Rename Movie FOLDER to match preferred MovieName + (YEAR)
{
char fn[FNAMSIZ], to[FNAMSIZ];
if (!strcmp(foldername, fmt_name_year(fn,&e))) return;	// No need to rename
strcpy(&strcpy(to,path)[foldername-path], fn);
exec_rename((char*)path,to);
foldername=(char*)strrchr(strcpy(path,to),'/')+1;		// point to final folder in passed path
}


static int exec_yad_form(const char *cmd, DYNAG *d)
{
FILE *f = popen(cmd,"r");
int c;
char w[256], *ww=w;
while ((c=fgetc(f))!=EOF)
	if (c==124) {*ww=0; d->put(strtrim(w)); ww=w;}
	else *(ww++)=c;
int err=pclose(f);
if (err<0) crash("yad crashed!");
return(err);
}

// <span foreground="blue" size="x-large">Blue text</span> is <i>cool</i>!


#define HI_Y "<span foreground='green' font_weight='ultrabold'><big><big>"
#define HI_N "</big></big></span>"

static int yad_form(char *pth, EM_KEY1 *e)
{
char cmd[1024], buf[512], mnm[100];
fmt_name_year(mnm,e);
strfmt(buf,"   imdb %s",pth);	// Text for user to cut&paste into google search (NOT a 'default' ImdbNum!)
strfmt(cmd,"yad --form " WIDTH "--title=\"Confirm Movie Number / Name\"");
strcat(cmd," --field=num --field=nam --formatted --selectable-labels");
strendfmt(cmd," --text=\"%s  %d\r  %s%s\r\r%s\r\"",
		HI_Y,e->e.imdb_num, mnm, HI_N, buf);
DYNAG d(0);
int err=exec_yad_form(cmd, &d);
if (err || d.ct!=2) return(NO);
char *s=(char*)d.get(0);
if (*s) api_name_from_number_s(e, s);
s=(char*)d.get(1);
if (*s) strcpy(e->e.nam, s);
return(YES);
}

int MVDIR::valid_tt_file(void)			// Create (user-confirmed) _ttNNNNN file if not already present
{
if (dti_tt!=NOTFND) return(YES);			// _ttNNNNN file was found and validated by constructor
if (e.e.imdb_num==0) api_number_from_name(foldername, &e);
for (int loop=0;loop<2;loop++)
	{
	EM_KEY pe;
	memmove(&pe,&e,sizeof(EM_KEY));
	if (!yad_form(foldername,&e)) return(NO);
	if (e.e.imdb_num==pe.imdb_num) break;
	}
write_tt_file(path,&e);
rarbg_del();
rename_file(dti_vid);								// ALWAYS present, but may not need to be renamed
if (dti_srt!=NOTFND) rename_file(dti_srt);	// *.srt and/or *.nfo might not be present
if (dti_nfo!=NOTFND) rename_file(dti_nfo);
rename_folder();			// If FOLDER gets renamed, 'path' and 'foldername' are adjusted accordingly
return(YES);
}


// ends up here with e.e.imdb_num==0 when 'mount' and attempt to view 46Gb drive in Disks
// (using values held in EM_KEY 'e' after all other user input)
int MVDIR::add_or_update_omdb(void)
{
//return(0);	// fixfix
if (e.e.imdb_num==0) return(97);
OMDB omdb;
EM_KEY1 ek;
e.filesz=biggest_vid_sz/100000000;
if (!e.e.rating) e.seen=0;
if (omdb.get((EM_KEY1*)memmove(&ek,&e,sizeof(EM_KEY1))))
	{
	if (strcmp(ek.e.nam,e.e.nam) || ek.e.rating!=e.e.rating || ek.seen!=e.seen || ek.filesz!=e.filesz)
		if (!omdb.upd(&e)) return(1);		// Reurn non-zero if we tried to update, but it didn't work
	return(0);	// We didn't do anything, so no error!
	}
int ret = !omdb.put(&e);	// we return non-zero if not okay
return(ret);
}

// Constructor performs a range of validation checks on the passed folder & contents thereof...
// Biggest file must be video > 100Mb
// IF _ttNNNNN exists (max 1 such file)
//     first line MUST match folder name including "(YYYY)"
//     Biggest (video), and latest of any *.srt files must match Foldername excluding "(YYYY)"
// IF *.nfo exists (max 1 such file)
//     if _ttNNNNN also exists, AND Ino specified in *.nfo, the numbers must match
// IF multiple *.srt files exist, only consider the latest-dated one
MVDIR::MVDIR(char *pth)
{
if (pth==NULL || strlen(pth)>=sizeof(path)) crash("Movie folder path invalid or missing");
strcpy(path,pth);
dt = new DIRTBL(path);
foldername=(char*)strrchr(path,'/')+1;		// point to final folder in passed path
memset(&e,0,sizeof(EM_KEY));
dti_tt=geti_tt();
dti_vid=geti_vid();
dti_srt=geti_srt();
dti_nfo=geti_nfo();
if (dti_tt!=NOTFND)
	{
	check_tt_file(((FILEINFO*)dt->get(dti_tt))->name);	// Does _ttNNNNN look okay, and match Foldername?
	check_base_filename(dti_vid);								// video file MUST exist, and have proper name
	if (dti_srt!=NOTFND) check_base_filename(dti_srt);	// If *.srt exists, must have proper name
	if (dti_nfo!=NOTFND) check_base_filename(dti_nfo);	// If *.nfo exists, must have proper name
	}
if (dti_nfo!=NOTFND) read_nfo();	// Check or set Ino if present in *.nfo
}

MVDIR::~MVDIR()
{
strquote(0,0);
delete dt;
}

static void yad_notification(const char *title, const char *text)
{
char cmd[512], buf[1000];
strfmt(cmd,"yad --text-info " WIDTH 
	"--text=\"<b><big><big>%s</big></big></b>\" --text-align=center --title=\"%s\"", text,title);
exec_cmd(cmd, buf, sizeof(buf));
}

// Return NOTFND if no rating folder, else return rating value
static int prv_rating(const char *folder, short *bd)
{
DIRSCAN d(folder,"_?.?");
FILEINFO fi;
char	*fn;
int	rating=NOTFND;
while (d.next(&fi))
	if ((fi.attr&DT_DIR)!=0 && ISDIGIT((fn=&fi.name[strlen(fi.name)-4])[1]) && ISDIGIT(fn[3]))
		if (rating==NOTFND)
			{
			rating=(fn[1]-'0')*10 + (fn[3]-'0');
			*bd=short_bd(fi.dttm);
			}
		else
			crash("Multiple rating folders!");
return(rating);
}

static void write_ratings_folder(const char *folder, int rating, int prv)
{
char	fn[256], old_fn[256];
strfmt(fn,"%s/_%1.1f",folder, 0.1 * rating);   // The FULL name of required rating folder
if (prv==NOTFND)
	{
	if (mkdir(fn, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) !=0)
		crash("Error writing %s rating folder",fn);
	}
else
	{
	exec_rename(strfmt(old_fn,"%s/_%1.1f",folder, 0.1 * prv), fn);
	utime(fn,NULL);
	}
}

static char *fix_ampersand(char *nam)	// Change any "&" in MovieName to "&amp" (as required by YAD)
{
for (char *p=nam; *p; p++)
	if (*p==AMPERSAND) {strins(&p[1],"amp;"); p+=4;}
return(nam);		// Return passed string address as a convenience
}

void MVDIR::set_rating(void)
{
char cmd[1024], buf[1024];
int prv=prv_rating(path, &e.seen);
fix_ampersand(fmt_name_year(buf, &e));
strfmt(cmd,"yad --scale --text=\"%s   %s%s\"", HI_Y, buf, HI_N);
strendfmt(cmd," --title=\"Movie Rating\" %s ", WIDTH);
strendfmt(cmd,"%s\"%d\" ", "--value=", (prv==NOTFND)?0:prv);  // Default 0.0 if no previous rating file
strendfmt(cmd,"%s", "--min-value=\"0\" --max-value=\"99\" –step=\"1\"");
strendfmt(cmd," %s", "--mark=1:10 --mark=2:20 --mark=3:30 --mark=4:40 --mark=5:50 --mark=6:60 --mark=7:70 --mark=8:80 --mark=9:90");
int err=exec_cmd(cmd, buf, sizeof(buf));
if (!err && (e.e.rating=a2i(buf,2))!=prv && e.e.rating!=0)
	{
	write_ratings_folder(path, e.e.rating, prv);
	e.seen=short_bd(calnow());
	}
}


static int backup_restore(void)
{
char pth[256];
QSettings qs;
OMDB omdb;
strcpy(pth,qs.value("bck_path").toString().toStdString().c_str());
if (!*pth) crash("No backup path configured!");
if (pth[strlen(pth)-1] != '/') strcat(pth,"/");
calfmt(strend(pth),"%C-%02O-%02D_%2T-%2I.bck",calnow());
return(0);
}

static void QSettings_init(void)
{
QCoreApplication::setOrganizationName("Softworks");
QCoreApplication::setApplicationName("SMDB");
}
int main(int argc, char* argv[])
{
int err=NO, opt=1;
int32_t deli;
QSettings_init();
if (argc==2)
	{
	char *p=argv[1];
	if (SAME2BYTES(p,"--")) p++;
	if (!strcmp(p,"-b")) opt=2;
	if (SAME2BYTES(p,"-d")) {opt=3; deli=tt_number_from_str(&p[2]);}
	}
try
	{
	switch (opt)
		{
		case 1:
			{
			MVDIR mv(argv[1]);
			if (mv.valid_tt_file()) mv.set_rating();
			err=mv.add_or_update_omdb();		// Won't get here unless everything looks okay
			}
			break;
		case 2:	// s this wanted at all?
			{
sjhlog("Shirly not!");
			err=backup_restore();
			}
		break;
		case 3:
			{
			OMDB omdb;
			if (omdb.del(deli)) printf("\nDeleted imdb_num:%d from OMDB.dbf\n",deli);
			else err=77;
			}
		break;
		}
   }
catch (int e)
	{
	err=e;
	}
if (err)
	{
	sjhlog("Failed with error %d",err);
   printf("Failed with error %d\r\n",err);
	}
return(err);
}

static void crash(const char *fmt,...)	// pop up a YAD notification using formatted string as TITLE
{
char ss[1024];
va_list va;
va_start(va,fmt);
_strnfmt(ss,sizeof(ss)-1,fmt,va);
yad_notification("Fatal Error!",ss);
throw(99);
}

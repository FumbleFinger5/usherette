#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include <string>

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <utime.h>

#include "pdef.h"
#include "cal.h"
#include "str.h"
#include "memgive.h"
#include "parm.h"
#include "smdb.h"
#include "omdb1.h"
#include "flopen.h"
#include "drinfo.h"
#include "dirscan.h"
#include "log.h"
#include "imdb.h"
#include "imdbf.h"
#include "exec.h"
#include "scan.h"
#include <cstdarg>
#include "qblob.h"
#include "my_json.h"
#include "tmdbc.h"


GtkBuilder *builder;
GtkWidget *window;
GtkWidget *GWinum;	// IMDB number - only editable if didn't get it from *.nfo or _tt
GtkWidget *GWinam;	// The "standard default" name of the movie as gotten by IMDB api
GtkWidget *GWunam;	// Potential user override movie name
GtkWidget *GWlabel;	// Potential user override movie name
GtkWidget *GWok;
GtkWidget *GWscale;
GtkAdjustment *GWadj;

GtkWidget *GWPartWatched;
GtkAdjustment *GWadj2;
GtkWidget *GWwatchLabel;	// Label above "Partwatch" spin control RED if non-zero
GtkWidget *GWcopy;	// copy contents of GWlabel into clipboard to paste into browser search


static OMZ eee;


static void MessageBox(const char *txt)
{
GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
GtkWidget *dialog;
dialog = gtk_message_dialog_new (GTK_WINDOW(window), flags, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", txt);
gtk_dialog_run (GTK_DIALOG (dialog));
gtk_widget_destroy(dialog);
}


static bool get_session_gui(char *session_id)
{
char wrk[256], request_token[64];
bool ok=get_request_token_url(wrk, request_token);

GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                           GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
                                           "Authenticate request_TOKEN by visiting...");
gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Authentication Confirmed", 1, NULL);
GtkWidget *content_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
GtkWidget *label = gtk_label_new(NULL); // Create the label without setting text directly
char *markup = g_markup_printf_escaped("<a href='%s'>%s</a>", wrk, wrk);
gtk_label_set_markup(GTK_LABEL(label), markup);
g_free(markup); // Free the markup string after setting it
gtk_container_add(GTK_CONTAINER(content_area), label);
gtk_widget_show_all(dialog);
if (gtk_dialog_run(GTK_DIALOG(dialog)) == 1) ok=true;
gtk_widget_destroy(dialog);

return(authenticate_request_token(request_token, session_id));
}

bool authenticate(char *session_id)
{
if (session_id_works(session_id)) return(true);
if (!get_session_gui(session_id)) return(false);
return(save_new_session(session_id));
}





static int orphans(void)		// list any movies in imdb.api but not smdb.mst
{									// imdb.api = every movie usherette ever looked up, maybe deleted / not Added to database
IMDB_API ia;					// smdb.mst = my actual movie database
DYNAG *du=ia.get_tbl();
OMDB1   sjh(true);
OM1_KEY om1;
bool again=false;
DYNAG *dq=new DYNAG(sizeof(int32_t));
while (sjh.scan_all(&om1,&again))
    dq->put(&om1.imno);
int i,j;
char wrk[128];
strfmt(wrk, "Record counts - %s:%d",Basename(ia.filename()),du->ct);
printf("%s  %s:%d\n",wrk,Basename(sjh.filename()),dq->ct);
printf("Movies in %s but not %s...\n",ia.filename(),Basename(sjh.filename()));
for (i=0;i<du->ct;i++)
    if (in_table(&j,du->get(i),dq->get(0),dq->ct,sizeof(int32_t),cp_int32_t)==NOTFND)
        {
        int32_t imno=*(int32_t*)(du->get(i));
        strcpy(wrk,ia.get(imno,"Title"));
        if (!wrk[0]) strcpy(wrk,"MISSING");
        printf("%-8d  %s\n",imno, wrk);
        }
printf("Movies in %s but not %s...\n",sjh.filename(),Basename(ia.filename()));
for (i=0;i<dq->ct;i++)
    if (in_table(&j,dq->get(i),du->get(0),du->ct,sizeof(int32_t),cp_int32_t)==NOTFND)
        {
        int32_t imno=*(int32_t*)(dq->get(i));
        sjh.get_om1(imno,&om1);
        strcpy(wrk,"?");
        sjh.rh2str(om1.mytitle,wrk);    // only non-blank if there's a custom name for movie
        printf("%-8d  %s\n",imno, wrk);
        }
delete du;
delete dq;
return(0);
}

static void retrieve_api_title(OMZ *zz, char *buf)
{
if (!*zz->title)
	{
	JBLOB_READER jb(buf);
	char *str=jb.get("Title");
	strncpy(zz->title,fix_colon(str),sizeof(zz->title));
//sjhlog("retrieve_api_title I%d [%s] %d",zz->k.imno,zz->title,zz->year);
	}
}

// Use API with ImdbNo for title + Year - called by read_nfo(MV.e) AND on_GWok_clicked(eEe)
static int api_name_from_number(OMZ *zz)
{
char buf8k[8192]; // Allow PLENTY of space for the ibmdb API call
api_all_from_number(zz->k.imno,buf8k);
retrieve_api_title(zz,buf8k);
zz->year=a2i(strquote(buf8k,"Year"),4);
return(*zz->title!=0 && valid_movie_year(zz->year));    // ok - imdb number is valid - EM_KEY name+Year filled in
}




static void crash(const char *fmt,...)	// pop up a YAD notification using formatted string as TITLE
{
char ss[1024];
va_list va;
va_start(va,fmt);
strnfmt(ss,sizeof(ss)-1,fmt,va);
Sjhlog("Fatal error!\r\n%s",ss);
char cmd[1024], buf[256];
exec_cmd(strfmt(cmd,"notify-send -u critical %c%s%c",CHR_QTDOUBLE,ss,CHR_QTDOUBLE),buf,sizeof(buf));
throw(99);
}

// Simple class to load directory contents into a DYNTBL
class DIRTBL : public DYNAG	 {
public:
DIRTBL	(const char *pth);
private:
};

static int fn_cdx(const char *n)		// Check if video filename ends with "CDn" for multi-part movie.
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

struct WATCH_HISTORY {short sseen; char rating;};

// Create/validate _ttNNNNNN, Rename folder/files, Manage _N.N MovieRating folder
class MVDIR {
public:
MVDIR(char *pth);
bool	get_numnam(OMZ *oz);	// YES = bulletproof (values confirmed by presence of _ttNNNNN)
void	set_numnam(OMZ *oz);
void 	set_rating(void);
void 	update_imdb(bool set_watch_history);
~MVDIR();
char	path[1024];		// Initially, FULLpath passed to constructor - MAY BE CHANGED BY RENAME LATER
char	*foldername;	// BaseName (excluding path) of the current folder, which contains ONE MOVIE
int	dti_nfo;

OMZ oK;
char titlE[64];

private:
void	update_om2(bool set_watch_history);
int	geti_tt(void);
int	geti_vid(void);
int	geti_srt(void);
int	geti_jpg(void);
int	geti_nfo(void);
void	check_tt_file(char *fn);		// Check _ttNNNNN matches FOLDERname
void	check_base_filename(int ii);	// Check *.mkv, *.srt, *.nfo FILEnames match _ttNNNNN
void	read_nfo(void);
void	rename_file(int dti);
void	rename_folder(void);
void	rarbg_del(void);
char	*tt_filename(void) {return(((FILEINFO*)dt->get(dti_tt))->name);};
void update_watch_history(OMDB1 *om1, int32_t imno);
DIRTBL   *dt;		// Table of (unpathed) FILENAMES found in the FOLDER passed to constructor
int	dti_vid, dti_srt, dti_tt, dti_jpg;	// biggest (video, jpg), (latest) *.srt, *.nfo, _tt*, or NOTFND
int64_t biggest_vid_sz=0;
int32_t imdb_upd=0;
WATCH_HISTORY wh={0,0};
};

MVDIR *mv;

// Rename fully-qualified file or folder. Return YES if successful, else NO (fatal error?)
static bool exec_rename(char *from, char *to)
{
char cmd[512], buf[256];
strfmt(cmd,"%s %c%s%c %c%s%c", "mv", CHR_QTDOUBLE,from,CHR_QTDOUBLE, CHR_QTDOUBLE,to,CHR_QTDOUBLE);
int err=exec_cmd(cmd,buf,sizeof(buf));
if (err) crash("Error renaming %s to %s",from,to);
return(!err);
}

// Does _ttNNNNN exist? If so, check folder name matches first line therein
int MVDIR::geti_tt(void)
{
int i, ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (i=0;i<dt->ct;i++)
	if (strlen(fi[i].name)>3 && SAME3BYTES(fi[i].name,"_tt"))
		{if (ii==NOTFND) ii=i; else crash("Multiple _ttNNNNN files");}
if (ii==NOTFND) return(NOTFND);
return(ii);			// NOTFND if no _ttNNNNN
}

int MVDIR::geti_vid(void)
{
biggest_vid_sz=0;
int ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (int i=0;i<dt->ct;i++)
	if (fi[i].size>biggest_vid_sz && drisvid(drext(fi[i].name)))
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
	if (SAME4BYTES(drext(fi[i].name),".srt") && fi[i].dttm>dttm)
		dttm=fi[ii=i].dttm;
return(ii);			// NOTFND if no *.srt file
}

int MVDIR::geti_jpg(void)
{
int	sz=0;
int ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (int i=0;i<dt->ct;i++)
	if (SAME4BYTES(drext(fi[i].name),".jpg") && fi[i].size>sz)
		if (!unwanted_filename(fi[i].name))
			sz=fi[ii=i].size;
return(ii);			// Feasibly NOTFND if there's no *.jpg
}

int MVDIR::geti_nfo(void)
{
int ii=NOTFND;
FILEINFO *fi=(FILEINFO*)dt->get(0);
for (int i=0;i<dt->ct;i++)
	if (SAME4BYTES(drext(fi[i].name),".nfo"))
		{if (ii==NOTFND) ii=i; else crash("Multiple *.nfo files");}
return(ii);			// NOTFND if no *.srt
}

void MVDIR::check_base_filename(int ii)	// Check *.mkv, *.srt, *.nfo FILEnames
{														// must match _ttNNNNN (already checked that FOLDERname does)
char *fn = ((FILEINFO*)dt->get(ii))->name;
int len=strlen(fn)-4;	// Ignore " (YYYY)" of FOLDERname and ".ext" of FILEname when checking
if (len!=strlen(foldername)-7 || strncmp(foldername,fn,len))
	crash("Unexpected filename %s\r\nconflicts with %s",fn,tt_filename());
}

static int any_non_digits(const char *p)
{
while (ISDIGIT(*p)) p++;
return(*p!=0); // If not pointing to EOS nullbyte, it must be a non-digit
}

// Called if _ttNNNNN exists, to fill in EM_KEY Ino and Name+Year
void MVDIR::check_tt_file(char *fn)	// 1st line should contain "MovieName (YYYY)""
{
int len, yr;
char s[256];
HDL f;
if ((len=strlen(fn))>=8 && len<=11 && SAME3BYTES(fn,"_tt") && !any_non_digits(&fn[3])
&& (oK.k.imno=a2l(&fn[3],0))>1000
&& (f=flopen(strfmt(s,"%s/%s",path,fn),"r"))!=NULL
&& flgetln(s,sizeof(s)-1,f)>=8)
	{
	flclose(f);
	if (!strcmp(s,foldername) && SAME2BYTES(&s[len=strlen(s)-7]," (") && (yr=a2i(&s[len+2],4))>1920)
		{
		oK.year=yr;
		strancpy(oK.title,s,len+1);
		return;
		}
	crash("Text file %s doesn't contain foldername",fn);
	}
crash("Error reading %s",fn);
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
	if (num==oK.k.imno) return;
	crash("ImdbNo in *.nfo (%d) doesn't match _tt%d",num,oK.k.imno);
	}
oK.k.imno=num;
if (!api_name_from_number(&oK)) crash("IMDB API rejects tt%d",num);
}

// p points to 4 characters before close bracket in foldername. Check if it's a valid year
static bool valid_year1(char *p, OMZ *oz)
{
if (valid_movie_year(oz->year=a2i(p,4)) && !a2err) return(true);
return(false);
}

// p points to open bracket in foldername. Check if there's a following CLOSE bracket preceded by CCYY
static bool valid_year(char *p, OMZ *oz)
{
int cb=stridxc(')',p);
if (cb>4 && valid_year1(&p[cb-4],oz)) return(true);
return(false);
}

int moviename_in_foldername(char *fn, OMZ *oz)
{
int i=strlen(fn)-7, j;
if ((j=stridxc('(',fn))!=NOTFND && valid_year(&fn[j], oz))
	{strancpy(oz->title,fn,j); return(YES);}
if (i<0) return(NO);		// (just in case it's a very short folder name)
for(i=j=0; (i=stridxc('.',&fn[j]))!=NOTFND && i<sizeof(oz->title)-5; j+=(i+1))
	{
	if (valid_year1(&fn[j+i+1], oz) && (fn[j+i+5]=='.' || !fn[j+i+5]))
		{strancpy(oz->title,fn,j+i+1); return(YES);}
	}
return(NO);
}

void MVDIR::rarbg_del(void)	// Delete any files in MovieFolder starting with (case-insensitive) "rarbg"
{
char *fn= ((FILEINFO*)dt->get(dti_vid))->name;

for (int i=0;i<dt->ct;i++)
	{
	if (i==dti_vid || i==dti_srt) continue;
	FILEINFO *fi=(FILEINFO*)dt->get(i);
	char fn[FNAMSIZ];
	if (unwanted_filename(fi->name))
		{
		if (unlink(strfmt(fn,"%s/%s",path,fi->name)))
			Sjhlog("Error deleting %s",fn);
		}
	}
}

static char *delete_square_bracket_text(char *n)
{
char *p=strchr(n,'['), *q;
if (p!=NULL && (q=strchr(p,']'))!=NULL)
	strdel(p,q-p+1);
const char *unwanted_prefix[] = {"www.Torrenting.com", "www.torrenting.org", NULL};
int i,j;
for (i=0;unwanted_prefix[i];i++)
	if (!strncasecmp(n,unwanted_prefix[i],j=strlen(unwanted_prefix[i])))
	{
		Sjhlog("delete prefix: %s",n);
		strdel(n,j);
	}
while (*n==SPACE || *n=='-') strdel(n,1);
return(n);
}


class APOSTROPHIZER {			// A class to dynamically allocate space for any number
public:
APOSTROPHIZER(const char *_ttl);
const char *get(void);
char	*fiddle(char *moviename);
//~APOSTROPHIZER();			// nothing for destructor to do!
private:
char ttl[80];
int	again, prv_added;
bool apostrophe_already_present;
};

// this fn REPLICATED in q3:window for user-not-steve rating changes  // NOT ANY MORE!!! (JAN 2024)
char* APOSTROPHIZER::fiddle(char *mn)		// Fiddle with passed movie name to get round CLI and API quirks
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

APOSTROPHIZER::APOSTROPHIZER(const char *_ttl)
{
fiddle(strcpy(ttl,_ttl));
again=prv_added=0;
apostrophe_already_present=(stridxc(CHR_QTSINGLE,ttl)!=NOTFND);
}

// Return offset of last letter of first word ending in 's' in passed string
// Caller adds previously-returned offset to passed address, so it's effectively NEXT apostrophizable word
static int apostrophizable(const char *p)
{
int letters_stepped_over=0, i, c;
for (i=0;!ISALPHA(p[i]);i++) {;}	// skip past any initial non-letters
for (i=0;(c=p[i])!=0;i++)	//  now find the next "word" (sequence of ALPHA) ending in 's' 
	{
	if (c=='s' && letters_stepped_over>0 && (p[i+1]==0 || (p[i+1]==PLUS)))
		return(i);
	if (ISALPHA(c)) letters_stepped_over++;
	else letters_stepped_over=0;
	}
return(NOTFND);
}

const char* APOSTROPHIZER::get(void)
{
if (again++)
	{
	if (apostrophe_already_present || again>4) return(NULL);
	if (prv_added>1) strdel(&ttl[prv_added],3);
	prv_added++;
	int pa=apostrophizable(&ttl[prv_added]);
	if (pa==NOTFND) return(NULL);
	char add[4]={'%','2','7',0};
	strins(&ttl[prv_added+pa],add);
	prv_added+=(pa);	// +1 so NEXT call gets pointer to terminating 's of this call
	}
return(ttl);
}

// Call API with title+Year to get ImdbNo
static int api_number_from_name(const char *fn, OMZ *oz)	// oz, not e (param)
{
char fixfn[256];
delete_square_bracket_text(strcpy(fixfn,fn));
if (moviename_in_foldername(fixfn,oz))
	{
	const char *ttl;
	APOSTROPHIZER ap(oz->title);
	while ((ttl=ap.get())!=NULLPTR)
		{
		char *p, buf8k[8192];
		if (api_all_from_name(ttl, oz->year, buf8k) && *(p=strquote(buf8k,"imdbID"))!=0 && SAME2BYTES(p,"tt") && (oz->k.imno=a2l(&p[2],0))!=0)
			{
			*oz->title=0;	// so next line will copy definitive MovieName into oz
			retrieve_api_title(oz,buf8k);	// oz.Year not updated, 'cos it MUST be correct in order for lookup to work!
	    	return(YES);
			}
		}
	}
return(oz->k.imno=0);	// zeroise in case we put (invalid) value in there during above processing
}

static void escape_ampersand(char *s)	// replace every occurence of "&" with "&amp;"
{
int p;
while ((p=stridxc('&',s))!=NOTFND) s[p]='\t';
while ((p=stridxc('\t',s))!=NOTFND) strins(strdel(&s[p],1),"&amp;");
} 


static char *fmt_name_year(char *s, OMZ *oz)
{
*s=0;
if (oz->year)
    strfmt(s,"%s (%d)",oz->title,oz->year); 
return(s);
}

// Create "definitive" _ttNNNNN containing my preferred MovieName
static void write_tt_file(const char *folder, OMZ *oz)
{
char fn[FNAMSIZ];
HDL f=flopen(strfmt(fn,"%s/_tt%d", folder, oz->k.imno),"w");
if (f==NULL) crash("Can't write %s (read-only access?)",fn);
fmt_name_year(fn,oz);
flputln(fn,f);
flclose(f);
}

void MVDIR::rename_file(int dti)	// Rename avi / srt / nfo FILE in 'dt' to match preferred MovieName
{
char *fn= ((FILEINFO*)dt->get(dti))->name;
int len=strlen(fn)-4;
if (len==strlen(oK.title) && !strncmp(oK.title,fn,len)) return;	// No need to rename
char from[FNAMSIZ], to[FNAMSIZ];
strfmt(from,"%s/%s",path,fn);
strfmt(to,"%s/%s%s",path,oK.title,drext(fn));
exec_rename(from,to);
}

void MVDIR::rename_folder(void)	// Rename Movie FOLDER to match preferred MovieName + (YEAR)
{
char fn[FNAMSIZ], to[FNAMSIZ];
if (!strcmp(foldername, fmt_name_year(fn,&oK))) return;	// No need to rename
strcpy(&strcpy(to,path)[foldername-path], fn);
//int p;
//while ((p=stridxs("&amp;",to))!=NOTFND) *strdel(&to[p],4)='&';
exec_rename((char*)path,to);
foldername=(char*)strrchr(strcpy(path,to),'/')+1;		// point to final folder in passed path
}

bool MVDIR::get_numnam(OMZ *oz)	// Return TRUE if _tt exists
{
bool ret=(dti_tt!=NOTFND); // YES if _ttNNNNN already exists, else NO
if (oK.k.imno==0) api_number_from_name(foldername, &oK);
oz->k.imno=oK.k.imno; strcpy(oz->title,oK.title); oz->year=oK.year; oz->k.rating=oK.k.rating;
return(ret);
}

void MVDIR::set_numnam(OMZ *oz)
{
if (dti_tt!=NOTFND) throw(99);	// can't call this if _ttNNNN already exists

oK.k.imno=oz->k.imno; strcpy(oK.title,oz->title); oK.year=oz->year;

write_tt_file(path,&oK);
rarbg_del();
rename_file(dti_vid);								// ALWAYS present, but may not need to be renamed
if (dti_srt!=NOTFND) rename_file(dti_srt);	// *.srt and/or *.nfo might not be present
if (dti_jpg!=NOTFND) rename_file(dti_jpg);	// *.jpg might not be present
if (dti_nfo!=NOTFND) rename_file(dti_nfo);
rename_folder();			// If FOLDER gets renamed, 'path' and 'foldername' are adjusted accordingly
update_imdb(true);				// This call ensures movie exists in old OmDB.dbf and new imdb.dbf
}

int valid_rating(const char *s)
{
if (!ISDIGIT(s[0]) || s[1]!='.' || !ISDIGIT(s[2])) return(NOTFND);
return((s[0]-'0')*10 + (s[2]-'0'));
}

// check the rating folder for a partWatch file
static void check_for_partwatch_file(const char *pfn, OM1_KEY *om)
{
int again=0;
DIRSCAN d(pfn,"watched*");
FILEINFO fi;
struct dirent *entry;
while ((entry=d.next(&fi))!=NULL)
//while (d.next(&fi))
	{
	if (again++) crash("multiple Partwatch files");
	char s[32];
//	strtrim(strancpy(s,fi.name,30));
	strtrim(strancpy(s,entry->d_name,30));
	if (strncasecmp(s,"watched",7)) crash("Bad partwatch file %s",fi.name);
	strdel(s,7); while (s[0]==SPACE || s[0]=='=') strdel(s,1);
	if (s[0]=='0') strdel(s,1);
	if (s[0]=='.') strdel(s,1);
	if (ISDIGIT(s[0])) om->partwatch=s[0]-'0';
	else om->partwatch=0;
	}
}

// Return NOTFND if no Rating folder, else return Rating value
static char prv_rating(const char *folder, OM1_KEY *om)
{
memset(om,0,sizeof(OM1_KEY));
DIRSCAN d(folder,"_?.?");
FILEINFO fi;
char	*fn;
char	rating=NOTFND;
struct dirent *entry;
while ((entry=d.next(&fi))!=NULL)
	if ((fi.attr&DT_DIR)!=0 && valid_rating(fn=&fi.name[strlen(fi.name)-3])!=NOTFND)
		{
		if (rating==NOTFND)
			{
			rating=om->rating=(fn[0]-'0')*10 + (fn[2]-'0');
			om->seen=fi.dttm;
			}
		else
			{
			crash("Multiple Rating folders!");
			}
		check_for_partwatch_file(fi.name, om);
		}
return(rating);
}


// Constructor performs a range of validation checks on the passed folder & contents thereof...
// Biggest file must be video > 100Mb
// IF _ttNNNNN exists (max 1 such file)
//     first line MUST match folder name including "(YYYY)"
//     Biggest (video), and latest of any *.srt files must match Foldername excluding "(YYYY)"
// IF *.nfo exists (max 1 such file)
//     if _ttNNNNN also exists, AND Ino specified in *.nfo, the numbers must match
// IF multiple *.srt files exist, only consider the latest-dated one
MVDIR::MVDIR(char *pth)		// MVDIR constructor
{
if (pth==NULL || strlen(pth)>=sizeof(path)) crash("Movie folder path invalid or missing");
strcpy(path,pth);
dt = new DIRTBL(path);
foldername=(char*)strrchr(path,'/')+1;		// point to final folder in passed path
memset(&oK,0,sizeof(OMZ));
dti_tt=geti_tt();
dti_vid=geti_vid();
dti_srt=geti_srt();
dti_jpg=geti_jpg();
dti_nfo=geti_nfo();
if (dti_tt!=NOTFND)
	{
	FILEINFO *fi=((FILEINFO*)dt->get(dti_tt));
	check_tt_file(fi->name);	// Does _ttNNNNN look okay, and match Foldername?
	check_base_filename(dti_vid);								// video file MUST exist, and have proper name
	if (dti_srt!=NOTFND) check_base_filename(dti_srt);	// If *.srt exists, must have proper name
	if (dti_jpg!=NOTFND) check_base_filename(dti_jpg);	// If *.srt exists, must have proper name
	if (dti_nfo!=NOTFND) check_base_filename(dti_nfo);	// If *.nfo exists, must have proper name
	}
if (dti_nfo!=NOTFND) read_nfo();	// Check or set Ino if present in *.nfo

OM1_KEY omk;
if (prv_rating(path, &omk)!=NOTFND)
	{
	oK.k.rating=omk.rating;
	oK.k.seen=omk.seen;
	oK.k.partwatch=omk.partwatch;
	}
}

MVDIR::~MVDIR()
{
strquote(0,0);
delete dt;
if (imdb_upd>0)
	{
	RECENT recent;
	recent.put(imdb_upd);
	}
}


static void update_api_api(OMZ *k)	// Update imdb.api (ImdbNo record contains ENTIRE text returned by API) 
{												// k points to private oK within MV
IMDB_API ia;
if (ia.get(k->k.imno,0)==NULL)
	{
	char buf8k[8192];    // Allow PLENTY of space for the ENTIRE ibmdb API call
	tmdb_all_from_number(k,buf8k);
	ia.put(k->k.imno,buf8k);
	}
}
static void update_api_dbf(int32_t imno)	// Update imdb.dbf (optimised storage for only the fields Q3 needs)
{
IMDB_FLD imf;
if (imf.exists(imno)) return;					// sjhLog("tt%d shouldn't exist",imno); 
char buf[8192];    // Allow PLENTY of space for the ENTIRE imdb API call
IMDB_API ia;
const char *ptr=ia.get(imno,0);
if (ptr==NULL) m_finish("Impossible!");		// Should have just Added IA.rec if not already present
imf.put(imno,ptr);
}

void MVDIR::update_watch_history(OMDB1 *om1, int32_t imno)
{
//const char *prv=om1->get_notes(imno);
std::string str=om1->get_notes(imno);
const char *prv=str.c_str();
int sz=strlen(prv)+1;
char *add=(char*)memgive(32);
calfmt(add,"%3.3M %4C",long_bd(wh.sseen));
strendfmt(add,"  %1.1f",0.1*wh.rating);
if (stridxs(add,prv)==NOTFND)       // ONLY add 'rating history' line if not already present
   {
   strcat(add,"\n");
   if (*prv)
      {
      strins(add,"\n");
      add=(char*)memrealloc(add,strlen(add)+sz);  // (sz = prv notes length + 1 for EOS)
      strins(add,prv);
      }
	om1->put_notes(imno,add);
   }
memtake(add); memtake(prv);
}

void MVDIR::update_om2(bool set_watch_history)	// could pass/populate optional non-null WATCHED ptr before overwriting
{
OMDB1 om1(true);
OM1_KEY k; memset(&k,0,sizeof(OM1_KEY));
if (!om1.get_om1(k.imno=imdb_upd,&k))
	{
	IMDB_API ia;
	k.tmno=oK.k.tmno;
	k.tv=oK.k.tv;
	k.rating=oK.k.rating;
	k.sz=biggest_vid_sz/100000000;
	k.added=short_bd(calnow());
	if (k.seen==0 && oK.k.seen!=0)
		{Sjhlog("Rating dttm override to %s",dmy_hm_str(k.seen=oK.k.seen));}
	const char *inam=ia.get(k.imno,get_fld_name(FID_TITLE));
	if (inam && *inam && !same_alnum(inam,oK.title))
		{k.mytitle=om1.str2rh(oK.title);}
	om1.put(&k);
	}
else if (set_watch_history)
	{
	wh.sseen=short_bd(k.seen);
	wh.rating=k.rating;
	}
// ELSE any non-zero k.rating is about to get zeroised before possibly being changed to new value GRAB IT!
if (oK.k.rating==0) oK.k.rating=k.rating;		// 2/6/24 - guessing this will stop unwanted loss of rating!
om1.put_rating(k.imno,oK.k.rating, &oK.k);	// re-watch history handled internally by OMDB1 
short sseen=short_bd(k.seen);
if (!set_watch_history)				// It's SECOND call - if watch_history WAS set, record in notes
	{
	if (wh.sseen!=0 && (sseen-wh.sseen)>31)
		update_watch_history(&om1,k.imno);	// deal with oK.k.partwatch in rating folder and OMDB1 database
	}
k.rating=oK.k.rating;
#ifdef KEEP_TMDB_SYNCHED		// maintain TMdb online ratings in real time
if (!tmdb_update_rating(authenticate, k.tmno, k.imno, k.tv, 0,0, k.rating))
	sjhlog("Error setting tmdb rating:%d on I:%d",k.imno);
//else sjhlog("Set rating:%d (tmdb:%d) on I:%d T:%d/%d",k.rating, rating2tmdb(k.rating), k.imno, k.tmno,k.tv);
#endif
}

static void align_partwatch_on_disc(const char *path, char partwatch)
{
bool unchanged=NO;
int i;
DIRSCAN ds(path, "watched*");
FILEINFO fi;
struct dirent *entry;
DYNAG d(sizeof(FILEINFO));  // Store FULLPATH as returned by ds.next(), not just filename in fi
while ((entry=ds.next(&fi))!=NULLPTR)
	{
	if ((entry->d_type&DT_DIR)!=0) continue;
    if (d.ct) m_finish("multiple watched!");
    char str[32];
    strancpy(str,&entry->d_name[7],30);
    if (str[0]==SPACE) strdel(str,1);
    if (str[0]=='0') strdel(str,1);
    if (str[0]=='.') strdel(str,1);
    if (!ISDIGIT(str[0])) m_finish("Bad PartWatch file:%s",fi.name);
    unchanged = ((str[0]-'0')==partwatch);
    d.put(&fi);
	}
if (unchanged || (d.ct==0 && partwatch==0)) return;
char from[FNAMSIZ], to[FNAMSIZ];
strfmt(to,"%s/watched 0.%c",path,partwatch+'0');
if (d.ct)
    {
    memmove(&fi,d.get(0),sizeof(FILEINFO));
    strcpy(from,fi.name);
    if (partwatch) exec_rename(from,to);
    else drunlink(from);
    return;
    }
// If we get here, there's no existing file, but partWatch is non-zero, so write a new file
HDL f=flopen(to,"w");
flclose(f);
}

static void align_partwatch_mst(int32_t imno, char partwatch)	// Ensure smdb.mst correctly reflects this imno+partwatch pair
{
USRTXT ut(imno);
//const char *prv=ut.get();
std::string str=ut.get();
const char *prv=str.c_str();
int i;
if ((i=stridxs("{watched=0.",prv))!=NOTFND)
    {
    if (prv[i+11]!=partwatch+'0')
        {
        char *upd=(char*)memadup(prv,strlen(prv)+1);
		if (partwatch) upd[i+11]=partwatch+'0';
		else strdel(&upd[i],13);
        ut.put(upd);
        memtake(upd);
        }
    return;
    }
if (!partwatch) return;	// No existing PartWatch, and no new non-zero value to be Added
char *upd=(char*)memadup(prv,strlen(prv)+32);
strendfmt(upd,"\n{watched=0.%c}",partwatch+'0');
ut.put(upd);
memtake(upd);
}

static void partwatch_update(const char *pfn, OM1_KEY *om)
{
align_partwatch_on_disc(pfn, om->partwatch);
align_partwatch_mst(om->imno, om->partwatch);
/*
// don't remember why i started writing this...
int again=0;
DIRSCAN d(pfn,"watched*");
FILEINFO fi;
while (d.next(&fi))

	;
	*/
}


void MVDIR::update_imdb(bool set_watch_history)
{
update_api_api(&oK);	// pass &oK to have TMdb+tv set
update_api_dbf(oK.k.imno);
imdb_upd=oK.k.imno;
oK.k.sz=biggest_vid_sz/100000000;
if (!oK.k.rating) oK.k.seen=0;
update_om2(set_watch_history);	// UniqCall
}

void MVDIR::set_rating(void)
{
OM1_KEY omk;
char prv=prv_rating(path, &omk);
oK.k.seen=omk.seen;
oK.k.partwatch=gtk_adjustment_get_value (GWadj2);
if (eee.k.rating) oK.k.rating=eee.k.rating;
if (oK.k.rating)
	{
	char	fn[256], old_fn[256];
	strfmt(fn,"%s/_%1.1f",path, 0.1 * oK.k.rating);   // The FULL name of required Rating folder
	if (prv==NOTFND)
		{
		if (mkdir(fn, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) !=0)
			crash("Error writing %s Rating folder",fn);
		}
	else
		{
		if (prv!=oK.k.rating)
			{
			exec_rename(strfmt(old_fn,"%s/_%1.1f",path, 0.1 * prv), fn);
			utime(fn,NULL);	// after rename, set DateLastModified to 'now'
			}
		}
	partwatch_update(fn,&oK.k);		// Add, rename, or delete "watched 0.n" in dbf AND rating folder
	if (prv!=oK.k.rating) oK.k.seen=calnow();
	}
else
	{
	if (prv<=0) oK.k.seen=0;
	}
update_imdb(false);				// This call updates RATING for movie in old OmDB.dbf and new imdb.dbf
}

static void show_watchlabel(int val)
{
char w[256];
bool bigger = (val!=0);
const char* colour=(bigger?"red":"grey");
strfmt(w,"<span foreground=\"%s\" size=\"%s\">", colour, bigger?"larger":"smaller");
strcat(w,"PartWatched");
strcat(w,"</span>");
gtk_label_set_markup(GTK_LABEL(GWwatchLabel), (const gchar*) w);
gtk_widget_set_visible(GWwatchLabel,true);
}

static void activate_partwatch_if_wanted(void)
{
bool can_partwatch=false;
if (gtk_adjustment_get_value(GWadj)>0) can_partwatch=true;
gtk_widget_set_sensitive(GWPartWatched,can_partwatch);
}

static void show_scale(bool show)
{
if (show)
	{
	char w[256];
	for (int i=0;i<10;i++)
		gtk_scale_add_mark((GtkScale*)GWscale, i, GTK_POS_TOP, (const char*)strfmt(w,"%d",i));
	gdouble gd=0.1 * eee.k.rating;
	gtk_adjustment_set_value (GWadj, gd);
	if (!mv->oK.k.partwatch)
		{
		USRTXT ut(mv->oK.k.imno);		// Opens ommdb.mst and reads any user notes record
//		const char *s=ut.get();
///		std::string str=ut.get();
///		const char *s=str.c_str();
		const char *s=ut.get().c_str();
		int i=stridxs("{watched",s);
		if (i!=NOTFND)
			{
			i=s[i+11]-'0';
			if (i<1 || i>9) m_finish("Bad partwatch in notes");
			mv->oK.k.partwatch=i;
			}
		}
	gtk_adjustment_set_value (GWadj2, mv->oK.k.partwatch);
	show_watchlabel(mv->oK.k.partwatch);
	}
gtk_widget_set_visible(GWscale,show);
activate_partwatch_if_wanted();
}

static int inum_unknown=NO;
static void show_inam(const char *colour)
{
char w[256], *e;
strfmt(w,"<span foreground=\"%s\" size=\"x-large\">", colour);
fmt_name_year(e=strend(w), &eee);	// Allow ampersand
escape_ampersand(e); 
strcat(w,"</span>");
gtk_label_set_markup(GTK_LABEL(GWinam), (const gchar*) w);
}

void set_button_image(GtkWidget *button, const char *file_path, int width, int height)
{
GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(file_path, width, height, true, NULL);
if (!pixbuf) crash("Error loading image: %s\n", file_path);
GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
g_object_unref(pixbuf); // Release the pixbuf after setting the image
gtk_button_set_image(GTK_BUTTON(button), image);
}

static int run_main(int argc, char *argv[])
{
gtk_init(&argc, &argv);

char w[256], *we;
int sz=readlink("/proc/self/exe", w, sizeof(w));
if (sz<10) throw(88);
w[sz]=0;
strncpy(we=strrchr(w,'/'),"/usherette.glade",20); // https://stackoverflow.com/questions/13237716/splash-screen-in-gtk
builder = gtk_builder_new_from_file(w);
window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
gtk_builder_connect_signals(builder, NULL);
GWinum = GTK_WIDGET(gtk_builder_get_object(builder, "GWinum"));
GWinam = GTK_WIDGET(gtk_builder_get_object(builder, "GWinam"));
GWunam = GTK_WIDGET(gtk_builder_get_object(builder, "GWunam"));
GWlabel = GTK_WIDGET(gtk_builder_get_object(builder, "GWlabel"));
GWscale = GTK_WIDGET(gtk_builder_get_object(builder, "GWscale"));
GWadj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "GWadj"));
GWok = GTK_WIDGET(gtk_builder_get_object(builder, "GWok"));

GWPartWatched = GTK_WIDGET(gtk_builder_get_object(builder, "GWPartWatched"));
GWadj2 = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "GWadj2"));
GWwatchLabel = GTK_WIDGET(gtk_builder_get_object(builder, "GWwatchLabel"));


strcpy(we,"/lens.png");
sjhlog("file [%s]", w);
GWcopy = GTK_WIDGET(gtk_builder_get_object(builder, "GWcopy"));
set_button_image(GWcopy, w, 25, 25); // Adjust width and height as needed
//GtkWidget *copyimage = gtk_image_new_from_file(w);
//gtk_button_set_image(GTK_BUTTON(GWcopy), copyimage);


MVDIR _mv(argv[1]);
mv=&_mv;
//gtk_window_set_title((GtkWindow*) window, mv->foldername);
strcpy(w,mv->foldername);
if (memcmp(argv[0],"/home",5)==0) strins(w,"(DEV) ");
gtk_window_set_title((GtkWindow*) window, w);

if (!mv->get_numnam(&eee))	// Populate eEe.num/nam. Return YES/NO = Does _ttNNNN exist?
	{
	if (eee.k.imno) show_inam("red");
	gtk_label_set_text(GTK_LABEL(GWlabel), (const gchar*) strfmt(w,"IMDB %s",mv->foldername));
	gint pos=0;
	strfmt(w,"tt%d",eee.k.imno);
	if (eee.k.imno==0) inum_unknown=YES;
	else gtk_editable_insert_text((GtkEditable*)GWinum, (const gchar*) w, 10, &pos);
	if (mv->dti_nfo!=NOTFND)	// can't change ImdbNum if it came from *.nfo
		gtk_editable_set_editable((GtkEditable*)GWinum, FALSE);
	show_scale(NO);
	}
else
	{
	show_inam("green");
	gtk_widget_hide(GWlabel);
	gtk_widget_hide(GWcopy);
	gtk_widget_hide(GWunam);
	gtk_widget_hide(GWinum);
	show_scale(YES);
	}

gtk_widget_set_sensitive(GWPartWatched,false);

gtk_widget_show(window);

gtk_main();
return EXIT_SUCCESS;
}

static void notify_error(int err)
{
const char *x="";
if (err==-500) x=" - Invalid folder path";
if (err==-393) x=" - Database system not active";
sjhLog("Failed with error %d%s",err,x);
printf("Failed with error %d%s\r\n",err,x);
}

static int valid_folder(const char *p)
{
struct stat sb;
char path[PATH_MAX];
drfullpath(path,p);			// Fully expand passed path
if (stat(path,&sb)) return(-500);	// some kind of error (copied to system global 'errno')
return(NO);	// No error
}

static void rebuild_dbf_cache(void)
{
sjhlog("Updating imdb.dbf...");
printf("Updating imdb.dbf...\r\n");
IMDB_API ia;
DYNAG *d=ia.get_tbl();
IMDB_FLD im;
int i=im.recct();
if (i) m_finish("imdb.fld already contains %d records! Can't rebuild!",i);
//int then=calnow();
for (i=0;i<d->ct;i++)
	{
	int32_t imno=*((int32_t*)d->get(i));
	const char *buf=ia.get(imno, NULL);
	im.put(imno,buf);
	}
//sjhlog("Wrote %d records",d->ct);
printf("Wrote %d records\r\n",d->ct);
delete d;
//int took=calnow()-then;
//printf("Took %d\r\n",took);
}

static void update_api_cache(const char *p)	// -u param may be followed by NNNN-NNNNN range of imddID's if not ALL
{
if (!*p) p="1-999999999";
OM1_KEY omk;
omk.imno = tt_number_from_str(p);
int32_t hi;
if (a2err && a2err_char=='-') hi=tt_number_from_str(&p[stridxc('-',p)+1]); else hi=omk.imno;
bool one=(omk.imno==hi);	// crash if only looking for ONE imno, and it's NOT in the cache database
IMDB_API ia;
int ct, tmr_ct=0;	// ct to prevent hitting the rate limit, tmr_ct for occasional screen update
HDL	tmr=tmrist(100);
int then=calnow();
OMDB1 om1(true);
for (ct=0; ++ct<800 && omk.imno<=hi && om1.get_ge(&omk); omk.imno++)
	{
	const char *p=ia.get(omk.imno,0);
	if (p==NULL)
		{
		char buf8k[8192];    // Allow PLENTY of space for the ENTIRE ibmdb API call
		if ((++tmr_ct & 3)==0)
			{
			if (tmr_ct>100) break;
			while (tmrelapsed(tmr)<0) usleep(100000);
			tmrreset(tmr,100);
			}
		if (!api_all_from_number(omk.imno, buf8k)) break;
		ia.put(omk.imno,buf8k);
		if ((tmr_ct&15)==0) printf("imno:%d ADD %zu bytes TOT=%d\n",omk.imno,strlen(buf8k),tmr_ct);
		}
	else if (one) sjhlog("tt%07d [%s]",omk.imno,p);
	}
tmrrls(tmr);
int took=calnow()-then;
printf("took %d\r\n",took);
}


char *str_unquote(char *s)
{
int len=strlen(s);
if (len>=2 && s[0]==CHR_QTDOUBLE && s[len-1]==CHR_QTDOUBLE)
	{s[len-1]=0; strdel(s,1);}
return(s);
}


char *str_quote_if_commas(char *s)
{
if (stridxc(COMMA,s)==NOTFND) return(str_unquote(s));
strendfmt(s,"%c",CHR_QTDOUBLE);
strinsc(s,CHR_QTDOUBLE);
return(s);
}

char *space_after_comma(char *s)
{
for (int i=1;s[i];i++)
	if (s[i]==COMMA && s[i+1]!=SPACE) strinsc(&s[i+1],SPACE);
return(s);
}


//Const,Your Rating,Date Rated,Title,URL,Title Type,IMDb Rating,Runtime (mins),Year,Genres,Num Votes,Release Date,Directors
//tt0100024,9,2016-03-28,Life Is Sweet,https://www.imdb.com/title/tt0100024/,movie,7.4,103,1990,"Comedy, Drama",11025,1990-11-15,Mike Leigh
static int xport(void)	// Update format of 'Watch History' subrecord within in Notes field
{
char str[256], s[128], imno[16];
int ct=0;
bool again=false;
OM1_KEY k;
OMDB1 om(true);
SCAN_ALL ia;
HDL f=flopen("/home/steve/Downloads/myratings.csv","w");
flputln(strcpy(str,"Const,Your Rating,Date Rated,Title,URL,Title Type,"
	"IMDb Rating,Runtime (mins),Year,Genres,Num Votes,Release Date,Directors"),f);
//flputln(strcpy(str,"imdbID,Rating10,WatchedDate"),f);
while (om.scan_all(&k,&again))
	{
int32_t ww=NO, want[]={3316948,1131729,100046,100140,100150,0};
for (int w=0;!ww && want[w];w++) ww=(want[w]==k.imno);
//if (!ww) continue;

	strfmt(imno,"tt%07d",k.imno);
//printf("%s\n",imno);
	if (k.rating<10) continue;		// hkmsrda7
	int rat=(k.rating+5)/10;
	if (k.rating>=93) rat=10;
	if (k.rating>=84 && k.rating<=89) rat=9;

rat=(rating2tmdb(k.rating)+5)/10;

	if (rat<1 || rat>10)
		m_finish("%s duff rating!", imno);
	if (k.seen==0) m_finish("%s duff date seen!",imno);
	strfmt(str,"%s,%d",imno,rat);
	calfmt(strend(str),",%4C-%02O-%02D",k.seen);
	ia.get(k.imno,FID_TITLE,s);
	strendfmt(str,",%s",str_quote_if_commas(s));
	strendfmt(str,",https://www.imdb.com/title/tt%07d/,movie,5.5",k.imno);
	ia.get(k.imno,FID_RUNTIME,s);
if ((ww=stridxc(':',s))==NOTFND) continue;
int mm=(a2i(s,0)*60) + a2i(&s[ww+1],0);
if (mm<60) continue; // ignore short movies 
	strendfmt(str,",%d",mm);
	ia.get(k.imno,FID_YEAR,s);
	int yr=a2i(s,4);
	strendfmt(str,",%s",s);
	ia.get(k.imno,FID_GENRE,s);
	strendfmt(str,",%s,12345",str_quote_if_commas(space_after_comma(s)));
strendfmt(str,",%4d-01-01",yr);
	ia.get(k.imno,FID_DIRECTOR,s);
	strendfmt(str,",%s",str_quote_if_commas(s));
	flputln(str,f);
	ct++;
//	if (ct>600) break;
	}
printf("Exported %d ratings\n",ct);
flclose(f);
return(0);	// No error
}

static int list_mytitle(void)
{
char str[256], s[128], imno[16];
int ct=0;
bool again=false;
OM1_KEY k;
OMDB1 om(true);
IMDB_API ia;
while (om.scan_all(&k,&again))
	{
	if (k.mytitle==0) continue;
	const char *inam=ia.get(k.imno,get_fld_name(FID_TITLE));
	strfmt(imno,"tt%07d",k.imno);
	om.rh2str(k.mytitle,str);
	printf("%-11.11s%s\n%11.11s%s\n",imno,str,"(imdb)  ",inam);
	ct++;
	}
printf("Listed %d movies with 'non-imdb' names\n",ct);
return(0);	// No error
}

struct NMG {char nm[30]; short ct;};
int _cdecl cp_str(char *a, char *b)
{
int cmp=strcmp(a,b);
if (cmp<0) return(-1);
if (cmp>0) return(1);
return(0);
}

static int list_genre(void)
{
char str[256], s[128], imno[16];
int i;
bool again=false;
OM1_KEY k;
OMDB1 om(true);
IMDB_API ia;
DYNTBL nmt(sizeof(NMG),(PFI_v_v)cp_str);
NMG nmg, *_n;
while (om.scan_all(&k,&again))
	{
	char genall[128], *_g;
	strcpy(genall,ia.get(k.imno,get_fld_name(FID_GENRE)));
strcat(genall,",");
	strxlt(genall,COMMA,TAB);
	for (i=0; (_g=vb_field(genall,i))!=NULL && *strtrim(strcpy(nmg.nm,_g));i++)
		{
		if ((_n=(NMG*)nmt.find(nmg.nm))!=NULL) _n->ct++;
		else {nmg.ct=1; nmt.put(&nmg);}
		}
	}
for (i=0;i<nmt.ct;i++)
	{
	_n=(NMG*)nmt.get(i);
	printf("%-5d  %s\n",_n->ct, _n->nm);
	}
return(0);	// No error
}


static int32_t get_bd(const char *p)
{
int32_t bd=caljoin(a2i(p,4),a2i(&p[5],2),a2i(&p[8],2),0,0,0);
char s[32];
calfmt(s, "%4C-%02O-%02D",bd);
if (strcmp(p,s)) return(0);
return(bd);
}

static int fix_added(const char *pp)	// if Added=0 set to watched date
{
char p[128]; strcpy(p,pp);
if (SAME2BYTES(p,"tt")) strdel(p,2);
int c=stridxc(COMMA,p);
int32_t imno=a2l(p,c), bd;
if (c<5 || (imno=a2l(p,c))<9999 || (bd=get_bd(&p[c+1]))==0)
	m_finish("Bad 'Date Added' parameter - expected -a[imno],YYYY-MM-DD");
OM1_KEY k;
OMDB1 om1(true);
if (!om1.get_om1(imno,&k)) m_finish("Error1 changing Date Added");
k.added=short_bd(bd);
if (!om1.upd(&k)) m_finish("Error2 changing Date Added");
printf("IMNO:TT%d Date Added changed to %s\n",imno,dmy_stri(short_bd(bd)));
return(0);	// No error
}

static int del(char *p)
{
int32_t deli = tt_number_from_str(&p[2]);
OMDB1 om1(true);
const char *fn=om1.filename();
if (om1.del(deli)) printf("\nDeleted imno:%d from %s\n",deli,fn);
else printf("\nFailed to delete imno:%d from %s\n",deli,fn);

IMDB_API ia;
fn=ia.filename();
if (ia.del(deli)) printf("\nDeleted imno:%d from %s\n",deli,fn);
else printf("\nFailed to delete imno:%d from %s\n",deli,fn);

IMDB_FLD imf;
fn=imf.filename();
if (imf.exists(deli) && imf.del(deli)) printf("\nDeleted imno:%d from %s\n",deli,fn);
else printf("\nFailed to delete imno:%d from %s\n",deli,fn);
return(0);
}

static int view(char *p)
{
//	exec_cmd_lf=true;
int32_t imno = tt_number_from_str(&p[2]);
IMDB_API ia;
const char *str=ia.get(imno,0);
printf("\n%s\n",str);
return(0);
}

static bool is_mount(char *p)
{
PATHDYNAG pd(p);
return(pd.is_mount());
}

static void test(void)
{
SCAN_ALL sa;
DYNAG d(0);
for (int i=0;i<10;i++)
	d.put("123456789");
//printf("tot:%d \n",d.total_size());
}

int main(int argc, char *argv[])
{
int err=0;
if (argc==2)
	{
	char *p=argv[1];
	if (SAME2BYTES(p,"--")) p++;
	if (SAME2BYTES(p,"-a")) return(fix_added(&p[2]));
	if (SAME2BYTES(p,"-d")) return(del(p));
	if (SAME2BYTES(p,"-g")) return(list_genre());
	if (SAME2BYTES(p,"-i")) {leak_tracker(YES); rebuild_dbf_cache(); leak_tracker(NO); return(0);}
	if (SAME2BYTES(p,"-l")) return(list_mytitle());
	if (SAME2BYTES(p,"-o")) return(orphans());
	if (SAME2BYTES(p,"-u")) {update_api_cache(&p[2]); return(0);}
	if (SAME2BYTES(p,"-v")) return(view(p));
	if (SAME2BYTES(p,"-x")) return(xport());
	}
if (argc==2 && !is_mount(argv[1])) crash("Path not mounted [%s]",argv[1]);
try
	{
	err=valid_folder(argv[1]);
	if (!err) err=run_main(argc,argv);
   }
catch (int e)
	{
	err=e;
	}
if (err)
	{
	if (argc>1) printf("\n[%s\n\n]",argv[1]);
	notify_error(err);
	}
return(err);
}

extern "C"
void  on_GWok_clicked(GtkButton *b)			// HERE is the on click Ok routine
{
if (gtk_widget_get_visible(GWscale))
	{
	mv->set_rating();
	gtk_window_close((GtkWindow*)window);	// Only close if Rating i/p (not if Num or Nam changed)
	return;
	}
char w[256];
strcpy(w,gtk_editable_get_chars((GtkEditable*)GWunam,0,NOTFND));
if (*w) strcpy(eee.title,w);
else *eee.title=0;
if (api_name_from_number(&eee))
	{
	if (inum_unknown)
		{
		show_inam("blue");
		gtk_label_set_selectable(GTK_LABEL(GWinam),TRUE);
		}
	else show_inam("cyan");
	gtk_widget_show(GWinam);
	if (inum_unknown)
		{
		inum_unknown=NO;
		return;
		}
	gtk_widget_hide(GWlabel);
	gtk_widget_hide(GWunam);
	mv->set_numnam(&eee);
	}
else		// 2 posibilities:	A - (no _tt file OR *.nfo) couldn't match exact foldername to a known moviename+year
	{		//							B - got valid imno (from _tt file OR *.nfo), but omdb doesn't have it (cf SouthPark Cred) 
//	sjhlog("### MovieFolder:%s \nCan't match to IMDBNO:%d",mv->foldername,eee.k.imno);	// by clicking OK without a valid movie foldername
   strfmt(w,"Missing / Invalid ImdbNO:%d",eee.k.imno);
	MessageBox(w);
	return;		// DON'T show the rating slider control
	}
show_scale(YES);
}

extern "C"
void  on_GWinum_changed(GtkEditable *editable)
{
char w[32];
strcpy(w,gtk_editable_get_chars(editable,0,NOTFND));
eee.k.imno=tt_number_from_str(w);
}
extern "C"
void  on_GWunam_changed(GtkEditable *editable)
{
strcpy(eee.title,gtk_editable_get_chars(editable,0,NOTFND));
}

extern "C"
void  on_GWadj_value_changed(GtkAdjustment *a)
{
gdouble gd;
gd=gtk_adjustment_get_value (a);
int i=(int)(gd*10);
eee.k.rating=(char)i;
activate_partwatch_if_wanted();
}

extern "C"
void  on_GWadj2_value_changed(GtkAdjustment *a)
{
int i=gtk_adjustment_get_value (a);
show_watchlabel(i);
}

static void google_search(const char *srch)
{
char browser[128], s[256];
strfmt(s,"%s%s","https://www.google.com/search?q=",srch);
int i;
while ((i=stridxc(SPACE,s))!=NOTFND) s[i]='+';
sjhlog("[%s]",s);
execute(parm_str("browser",browser, "xdg-open"),s);
}

extern "C"
void on_GWcopy_clicked(void)
{
const char *txt;
GtkClipboard *clipboard;
txt = gtk_label_get_text(GTK_LABEL(GWlabel));
clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
gtk_clipboard_set_text(clipboard, txt, -1);
google_search(txt);
}

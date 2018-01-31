/*

GENERAL
This program converts bilevel PBM, 8-bit PGM, 24-bit PPM, and 32-bit
CMYK PAM files (output by Ghostscript as "pbmraw", "pgmraw", "ppmraw",
and "pamcmyk32" respectively) to HBPL version 1 for the consumption
of various Dell, Epson, and Fuji-Xerox printers.

With this utility, you can print to some Dell and Fuji printers, such as these:
    - Dell 1250c			B/W and Color
    - Dell C1660			B/W and Color
    - Dell C1760			B/W and Color
    - Epson AcuLaser C1700		B/W and Color
    - Fuji-Xerox DocuPrint CP105	B/W and Color

AUTHORS
This program was originally written by Dave Coffin in March 2014.

LICENSE
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

If you want to use this program under different license conditions,
then contact the author for an arrangement.

*/

static char Version[] = "$Id: foo2hbpl1.c,v 1.3 2014/03/30 05:08:32 rick Exp $";

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#ifdef linux
    #include <sys/utsname.h>
#endif

/*
 * Command line options
 */
int	MediaCode = 0;
char	*Username = NULL;
char	*Filename = NULL;
int	Clip[] = { 8,8,8,8 };

void
usage(void)
{
    fprintf(stderr,
"Usage:\n"
"   foo2hbpl1 [options] <pamcmyk32-file >hbpl-file\n"
"\n"
"	Convert Ghostscript pbmraw, pgmraw, ppmraw, or pamcmyk32\n"
"	format to HBPLv1, for the Dell C1660w and other printers.\n"
"\n"
"	gs -q -dBATCH -dSAFER -dQUIET -dNOPAUSE \\ \n"
"		-sPAPERSIZE=letter -r600x600 -sDEVICE=pamcmyk32 \\ \n"
"		-sOutputFile=- - < testpage.ps \\ \n"
"	| foo2hbpl1 >testpage.zc\n"
"\n"
"Options:\n"
"-m media	Media code to send to printer [1 or 6]\n"
"		  1=plain, 2=bond, 3=lwcard, 4=lwgcard, 5=labels,\n"
"		  6=envelope, 7=recycled, 8=plain2, 9=bond2,\n"
"		  10=lwcard2, 11=lwgcard2, 12=recycled2\n"
"-u left,top,right,bottom\n"
"		Erase margins of specified width [%d,%d,%d,%d]\n"
"-J filename	Filename string to send to printer\n"
"-U username	Username string to send to printer\n"
"-V		Version %s\n"
	, Clip[0], Clip[1], Clip[2], Clip[3]
	, Version);
}

void
error(int fatal, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fatal) exit(fatal);
}

struct stream
{
    unsigned char *buf;
    int size, off, bits;
};

void
putbits(struct stream *s, unsigned val, int nbits)
{
    if (s->off + 16 > s->size &&
	!(s->buf = realloc(s->buf, s->size += 0x100000)))
	    error (1, "Out of memory\n");
    if (s->bits)
    {
	s->off--;
	val |= s->buf[s->off] >> (8-s->bits) << nbits;
	nbits += s->bits;
    }
    s->bits = nbits & 7;
    while ((nbits -= 8) > 0)
	s->buf[s->off++] = val >> nbits;
    s->buf[s->off++] = val << -nbits;
}

/*
   Runlengths are integers between 1 and 17057 encoded as follows:

	1	00
	2	01 0
	3	01 1
	4	100 0
	5	100 1
	6	101 00
	7	101 01
	8	101 10
	9	101 11
	10	110 0000
	11	110 0001
	12	110 0010
	   ...
	25	110 1111
	26	111 000 000
	27	111 000 001
	28	111 000 010
	29	111 000 011
	   ...
	33	111 000 111
	34	111 001 000
	   ...
	41	111 001 111
	42	111 010 000
	50	111 011 0000
	66	111 100 00000
	98	111 101 000000
	162	111 110 000000000
	674	111 111 00000000000000
	17057	111 111 11111111111111
*/
void
put_len(struct stream *s, unsigned val)
{
    unsigned code[] =
    {
	  1, 0, 2,
	  2, 2, 3,
	  4, 8, 4,
	  6, 0x14, 5,
	 10, 0x60, 7,
	 26, 0x1c0, 9,
	 50, 0x3b0, 10,
	 66, 0x780, 11,
	 98, 0xf40, 12,
	162, 0x7c00, 15,
	674, 0xfc000, 20,
	17058
    };
    int c = 0;

    if (val < 1 || val > 17057) return;
    while (val >= code[c+3]) c += 3;
    putbits(s, val-code[c] + code[c+1], code[c+2]);
}

/*
   CMYK byte differences are encoded as follows:

	 0	000
	+1	001
	-1	010
	 2	011s0	s = 0 for +, 1 for -
	 3	011s1
	 4	100s00
	 5	100s01
	 6	100s10
	 7	100s11
	 8	101s000
	 9	101s001
	    ...
	 14	101s110
	 15	101s111
	 16	110s00000
	 17	110s00001
	 18	110s00010
	    ...
	 46	110s11110
	 47	110s11111
	 48	1110s00000
	 49	1110s00001
	    ...
	 78	1110s11110
	 79	1110s11111
	 80	1111s000000
	 81	1111s000001
	    ...
	 126	1111s101110
	 127	1111s101111
	 128	11111110000
*/
void
put_diff(struct stream *s, signed char val)
{
    static unsigned short code[] =
    {
	 2,  3, 3, 1,
	 4,  4, 3, 2,
	 8,  5, 3, 3,
	16,  6, 3, 5,
	48, 14, 4, 5,
	80, 15, 4, 6,
	129
    };
    int sign, abs, c = 0;

    switch (val)
    {
    case  0:  putbits(s, 0, 3);  return;
    case  1:  putbits(s, 1, 3);  return;
    case -1:  putbits(s, 2, 3);  return;
    }
    abs = ((sign = val < 0)) ? -val:val;
    while (abs >= code[c+4]) c += 4;
    putbits(s, code[c+1], code[c+2]);
    putbits(s, sign, 1);
    putbits(s, abs-code[c], code[c+3]);
}

void
setle(unsigned char *c, int s, int i)
{
    while (s--)
    {
	*c++ = i;
	i >>= 8;
    }
}

void
start_doc(int color)
{
    char reca[] = { 0x41,0x81,0xa1,0x00,0x82,0xa2,0x07,0x00,0x83,0xa2,0x01,0x00 };
    time_t t;
    struct tm *tmp;
    char datestr[16], timestr[16];
    char cname[128] = "My Computer";
    char *mname[] =
    {	"",
	"NORMAL",
	"THICK",
	"HIGHQUALITY",
	"COAT2",
	"LABEL",
	"ENVELOPE",
	"RECYCLED",
	"NORMALREV",
	"THICKSIDE2",
	"HIGHQUALITYREV",
	"COATEDPAPER2REV",
	"RECYCLEREV",
    };

    t = time(NULL);
    tmp = localtime(&t);
    strftime(datestr, sizeof datestr, "%m/%d/%Y", tmp);
    strftime(timestr, sizeof timestr, "%H:%M:%S", tmp);

    #ifdef linux
    {
	struct utsname u;

	uname(&u);
	strncpy(cname, u.nodename, 128);
	cname[127] = 0;
    }
    #endif

/* Lines end with \n, not \r\n */

    printf(
	"\033%%-12345X@PJL SET STRINGCODESET=UTF8\n"
	"@PJL COMMENT DATE=%s\n"
	"@PJL COMMENT TIME=%s\n"
	"@PJL COMMENT DNAME=%s\n"
	"@PJL JOB MODE=PRINTER\n"
	"@PJL SET JOBATTR=\"@LUNA=%s\"\n"
	"@PJL SET JOBATTR=\"@TRCH=OFF\"\n"
	"@PJL SET DUPLEX=OFF\n"
	"@PJL SET BINDING=LONGEDGE\n"
	"@PJL SET IWAMANUALDUP=OFF\n"
	"@PJL SET JOBATTR=\"@MSIP=%s\"\n"
	"@PJL SET RENDERMODE=%s\n"
	"@PJL SET ECONOMODE=OFF\n"
	"@PJL SET RET=ON\n"
	"@PJL SET JOBATTR=\"@IREC=OFF\"\n"
	"@PJL SET JOBATTR=\"@TRAP=ON\"\n"
	"@PJL SET JOBATTR=\"@JOAU=%s\"\n"
	"@PJL SET JOBATTR=\"@CNAM=%s\"\n"
	"@PJL SET COPIES=1\n"
	"@PJL SET QTY=1\n"
	"@PJL SET PAPERDIRECTION=SEF\n"
	"@PJL SET RESOLUTION=600\n"
	"@PJL SET BITSPERPIXEL=8\n"
	"@PJL SET JOBATTR=\"@DRDM=XRC\"\n"
	"@PJL SET JOBATTR=\"@TSCR=11\"\n"
	"@PJL SET JOBATTR=\"@GSCR=11\"\n"
	"@PJL SET JOBATTR=\"@ISCR=12\"\n"
	"@PJL SET JOBATTR=\"@TTRC=11\"\n"
	"@PJL SET JOBATTR=\"@GTRC=11\"\n"
	"@PJL SET JOBATTR=\"@ITRC=12\"\n"
	"@PJL SET JOBATTR=\"@TCPR=11\"\n"
	"@PJL SET JOBATTR=\"@GCPR=11\"\n"
	"@PJL SET JOBATTR=\"@ICPR=12\"\n"
	"@PJL SET JOBATTR=\"@TUCR=11\"\n"
	"@PJL SET JOBATTR=\"@GUCR=11\"\n"
	"@PJL SET JOBATTR=\"@IUCR=12\"\n"
	"@PJL SET JOBATTR=\"@BSPM=OFF\"\n"
	"@PJL SET JOBATTR=\"@TDFT=0\"\n"
	"@PJL SET JOBATTR=\"@GDFT=0\"\n"
	"@PJL SET JOBATTR=\"@IDFT=0\"\n"
	"@PJL ENTER LANGUAGE=HBPL\n"
	, datestr, timestr
	, Filename ? Filename : ""
	, Username ? Username : ""
	, mname[MediaCode]
	, color ? "COLOR" : "GRAYSCALE"
	, Username ? Username : ""
	, cname);
    fwrite (reca, 1, sizeof reca, stdout);
}

#define IP (((int *)image) + off)
#define CP (((char *)image) + off)
#define DP (((char *)image) + off*deep)
#define BP(x) ((blank[(off+x) >> 3] << ((off+x) & 7)) & 128)
#define put_token(s,x) putbits(s, huff[hsel][x] >> 4, huff[hsel][x] & 15)

void
encode_page(int color, int width, int height, char *image)
{
    unsigned char head[90] =
    {
	0x43,0x91,0xa1,0x00,0x92,0xa1,0x01,0x93,0xa1,0x01,0x94,0xa1,
	0x00,0x95,0xc2,0x00,0x00,0x00,0x00,0x96,0xa1,0x00,0x97,0xc3,
	0x00,0x00,0x00,0x00,0x98,0xa1,0x00,0x99,0xa4,0x01,0x00,0x00,
	0x00,0x9a,0xc4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x9b,
	0xa1,0x00,0x9c,0xa1,0x01,0x9d,0xa1,0x00,0x9e,0xa1,0x02,0x9f,
	0xa1,0x05,0xa0,0xa1,0x08,0xa1,0xa1,0x00,0xa2,0xc4,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x51,0x52,0xa3,0xa1,0x00,0xa4,
	0xb1,0xa4
    };
    unsigned char body[52] =
    {
	0x20,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x10,0x32,0x04,0x00,
	0xa1,0x42,0x00,0x00,0x00,0x00,0xff
    };
    static short papers[] =
    {	// Official sizes to nearest 1/600 inch
	// will accept +-1.5mm (35/600 inch) tolerance
	  0, 5100, 6600,	// Letter
	  2, 5100, 8400,	// Legal
	  4, 4961, 7016,	// A4
	  6, 4350, 6300,	// Executive
	 13, 2475, 5700,	// #10 envelope
	 15, 2325, 4500,	// Monarch envelope
	 17, 3827, 5409,	// C5 envelope
	 19, 2599, 5197,	// DL envelope
//	 ??, 4158, 5906,	// B5 ISO
	 22, 4299, 6071,	// B5 JIS
	 30, 3496, 4961,	// A5
	410, 5100, 7800,	// Folio
    };
    static const unsigned short huff[2][8] =
    {
	{ 0x01,0x63,0x1c5,0x1d5,0x1e5,0x22,0x3e6 }, // for text & graphics
	{ 0x22,0x63,0x1c5,0x1d5,0x1e5,0x01,0x3e6 }, // for images
    };
    unsigned char *blank;
    static int pagenum = 0;
    struct stream stream[5] = { { 0 } };
    int dirs[] = { -1,0,-1,1,2 }, rotor[] = { 0,1,2,3,4 };
    int i, j, row, col, deep, dir, run, try, bdir, brun, total;
    int paper = 510, hsel = 0, off = 0, bit = 0, stat = 0;
    int margin = width-96;

    for (i = 0; i < sizeof papers / sizeof *papers; i++)
	if (abs(width-papers[i+1]) < 36 && abs(height-papers[i+2]) < 36)
	    paper = papers[i];
    if (!MediaCode)
	MediaCode = paper & 1 ? 6 : 1;
    if (!pagenum)
	start_doc(color);
    head[12] = paper >> 1;
    if (paper == 510)
    {
	setle (head+15, 2,  (width*254+300)/600);  // units of 0.1mm
	setle (head+17, 2, (height*254+300)/600);
	head[21] = 2;
    }
    width = -(-width & -8);
    setle (head+33, 4, ++pagenum);
    setle (head+39, 4, width);
    setle (head+43, 4, height);
    setle (head+70, 4, width);
    setle (head+74, 4, height);
    head[55] = 9 + color*130;
    if (color)	body[6] = 1;
    else	body[4] = 8;

    deep = 1 + color*3;
    for (i=1; i < 5; i++)
	dirs[i] -= width;
    if (!color) dirs[4] = -8;

    blank = calloc(height+2, width/8);
    memset (blank++, -color, width/8+1);
    for (row = 1; row <= height; row++)
    {
	for (col = deep; col < deep*2; col++)
	    image[row*width*deep + col] = -1;
	for (col = 8; col < width*deep; col += 4)
	    if (*(int *)(image + row*width*deep + col))
	    {
		for (col = 12; col < margin/8; col++)
		    blank[row*(width/8)+col] = -1;
		blank[row*(width/8)+col] = -2 << (~margin & 7);
		break;
	    }
    }
    memset (image, -color, (width+1)*deep);
    image += (width+1)*deep;
    blank += width/8;

    while (off < width * height)
    {
	for (bdir = brun = dir = 0; dir < 5; dir++)
	{
	    try = dirs[rotor[dir]];
	    for (run = 0; run < 17057; run++, try++)
	    {
		if (color)
		{
		    if (IP[run] != IP[try]) break;
		}
		else
		    if (CP[run] != CP[try]) break;

		if (BP(run) != BP(try)) break;
	    }
	    if (run > brun)
	    {
		bdir = dir;
		brun = run;
	    }
	}
	if (brun == 0)
	{
	    put_token(stream, 5);
	    for (i = 0; i < deep; i++)
		put_diff(stream+1+i, DP[i] - DP[i-deep]);
	    bit = 0;
	    off++;
	    stat--;
	    continue;
	}
	if (brun > width * height - off)
	    brun = width * height - off;
	if (bdir)
	{
	    j = rotor[bdir];
	    for (i = bdir; i; i--)
		rotor[i] = rotor[i-1];
	    rotor[0] = j;
	}
	if ((off-1+brun)/width != (off-1)/width)
	{
	    if (abs(stat) > 8 && ((stat >> 31) & 1) != hsel)
	    {
		hsel ^= 1;
		put_token(stream, 6);
	    }
	    stat = 0;
	}
	stat += bdir == bit;
	put_token(stream, bdir - bit);
	put_len(stream, brun);
	bit = brun < 17057;
	off += brun;
    }

    putbits(stream, 0xff, 8);
    for (total = 48, i = 0; i <= deep; i++)
    {
	putbits(stream+i, 0xff, 8);
	stream[i].off--;
	setle (body+32 + i*4, 4, stream[i].off);
	total += stream[i].off;
    }
    head[85] = 0xa2 + (total > 0xffff)*2;
    setle (head+86, 4, total);
    fwrite(head, 1, 88+(total > 0xffff)*2, stdout);
    fwrite(body, 1, 48, stdout);
    for (i = 0; i <= deep; i++)
    {
	fwrite(stream[i].buf, 1, stream[i].off, stdout);
	free(stream[i].buf);
    }
    free(blank-width/8-1);
    printf("SD");
}
#undef IP
#undef CP
#undef DP
#undef BP
#undef put_token

int
getint(FILE *fp)
{
    int c, ret;

    for (;;)
    {
	while (isspace(c = fgetc(fp)));
	if (c == '#')
	    while (fgetc(fp) != '\n');
	else break;
    }
    if (!isdigit(c)) return -1;
    for (ret = c-'0'; isdigit(c = fgetc(fp)); )
	ret = ret*10 + c-'0';
    return ret;
}

void
do_file(FILE *fp)
{
    int type, iwide, ihigh, ideep, imax, ibyte;
    int wide, deep, byte, row, col, i, k;
    char tupl[128], line[128];
    unsigned char *image, *sp, *dp;

    while ((type = fgetc(fp)) != EOF)
    {
	type = ((type - 'P') << 8) | fgetc(fp);
	tupl[0] = iwide = ihigh = ideep = deep = imax = ibyte = -1;
	switch (type)
	{
	case '4':
	    deep = 1 + (ideep = 0);
	    goto six;
	case '5':
	    deep = ideep = 1;
	    goto six;
	case '6':
	    deep = 1 + (ideep = 3);
six:	    iwide = getint(fp);
	    ihigh = getint(fp);
	    imax = type == '4' ? 255 : getint(fp);
	    break;
	case '7':
	    do
	    {
		if (!fgets(line, 128, fp)) goto fail;
		if (!strncmp(line, "WIDTH ",6))
		    iwide = atoi(line + 6);
		if (!strncmp(line, "HEIGHT ",7))
		    ihigh = atoi(line + 7);
		if (!strncmp(line, "DEPTH ",6))
		    deep = ideep = atoi(line + 6);
		if (!strncmp(line, "MAXVAL ",7))
		    imax = atoi(line + 7);
		if (!strncmp(line, "TUPLTYPE ",9))
		    strcpy (tupl, line + 9);
	    } while (strcmp(line, "ENDHDR\n"));
	    if (ideep != 4 || strcmp(tupl, "CMYK\n")) goto fail;
	    break;
	default:
	    goto fail;
	}
	if (iwide <= 0 || ihigh <= 0 || imax != 255) goto fail;
	wide = -(-iwide & -8);
        if (ideep)
	    ibyte = iwide * ideep;
	else
	    ibyte = wide >> 3;
	byte = wide * deep;
	image = calloc (ihigh+2, byte);
	for (row = 1; row <= ihigh; row++)
	{
	    i = fread (image, ibyte, 1, fp);
	    sp = image;
	    dp = image + row*byte;
	    for (col = 0; col < iwide; col++)
	    {
		dp += deep;
		switch (ideep)
		{
		case 0:
		    *dp = ((image[col >> 3] >> (~col & 7)) & 1) * 255;
		    break;
		case 1:
		    *dp = ~*sp;
		    break;
		case 3:
		    for (k = sp[2], i = 0; i < 2; i++)
			if (k < sp[i]) k = sp[i];
		    *dp = ~k;
		    for (i = 0; i < 3; i++)
			dp[i+1] = k ? (k - sp[i]) * 255 / k : 255;
		    break;
		case 4:
		    for (i=0; i < 4; i++)
			dp[i] = sp[((i-1) & 3)];
		    break;
		}
		sp += ideep;
	    }
	    for (i = 0; i < deep*Clip[0]; i++)
		image[row*byte + deep+i] = 0;
	    for (i = deep*(iwide-Clip[2]); i < byte; i++)
		image[row*byte + deep+i] = 0;
	}
	memset(image+deep, 0, byte*(Clip[1]+1));
	memset(image+deep + byte*(ihigh-Clip[3]+1), 0, byte*Clip[3]);
	encode_page(deep > 1, iwide, ihigh, (char *) image);
	free(image);
    }
    return;
fail:
    fprintf (stderr, "Not an acceptable PBM, PPM or PAM file!!!\n");
}

int
main(int argc, char *argv[])
{
    int	c, i;

    while ( (c = getopt(argc, argv, "m:u:J:U:V")) != EOF)
	switch (c)
	{
	case 'm':  MediaCode = atoi(optarg); break;
	case 'u':  if (sscanf(optarg, "%d,%d,%d,%d",
			Clip, Clip+1, Clip+2, Clip+3) != 4)
		      error(1, "Must specify four clipping margins!\n");
		   break;
	case 'J':  if (optarg[0]) Filename = optarg; break;
	case 'U':  if (optarg[0]) Username = optarg; break;
	case 'V':  printf("%s\n", Version); return 0;
	default:   usage(); return 1;
	}

    argc -= optind;
    argv += optind;

    if (argc == 0)
    {
	do_file(stdin);
    }
    else
    {
	for (i = 0; i < argc; ++i)
	{
	    FILE *ifp;

	    if (!(ifp = fopen(argv[i], "r")))
		error(1, "Can't open '%s' for reading\n", argv[i]);
	    do_file(ifp);
	    fclose(ifp);
	}
    }
    puts("B\033%-12345X@PJL EOJ");
    return 0;
}

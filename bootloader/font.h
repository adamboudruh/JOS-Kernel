//https://www.win.tue.nl/~aeb/linux/kbd/font-formats-1.html
#include <uefi.h>

#define PSF1_FONT_MAGIC 0x0436
 
typedef struct {
    uint16_t magic; // Magic bytes for idnetiifcation.
    uint8_t fontMode; // PSF font mode
    uint8_t characterSize; // PSF character size.
    uint8_t data[];
} PSF1_Header;
 
#define PSF_FONT_MAGIC 0x864ab572
 
typedef struct {
    uint32_t magic;         /* magic bytes to identify PSF */
    uint32_t version;       /* zero */
    uint32_t headersize;    /* offset of bitmaps in file, 32 */
    uint32_t flags;         /* 0 if there's no unicode table */
    uint32_t numglyph;      /* number of glyphs */
    uint32_t bytesperglyph; /* size of each glyph */
    uint32_t height;        /* height in pixels */
    uint32_t width;         /* width in pixels */
    uint8_t data[];
} PSF_font;

PSF_font* load_font(char* fname){
    FILE* fnt_file=fopen(fname,"r");
    struct stat st;
    fstat(fnt_file,&st);
    printf("font size: %d\n",st.st_size);
    PSF_font* loaded_font = malloc(st.st_size);
    fread(loaded_font,st.st_size,1,fnt_file);
    fclose(fnt_file);

    if(loaded_font->magic==PSF_FONT_MAGIC){
        printf("Valid psf2 font found\n");
        return (PSF_font*)loaded_font;
    }
    else if (((PSF1_Header*)loaded_font)->magic==PSF1_FONT_MAGIC){
        PSF1_Header* old=(PSF1_Header*)loaded_font;
        printf("Valid psf1 font found\n");
        //convert to psf2 format
        PSF_font* font=malloc(st.st_size+28);
        font->magic=PSF_FONT_MAGIC;
        font->version=0;
        font->headersize=32;
        font->flags=0;
        font->numglyph=256;
        font->bytesperglyph=old->characterSize;
        font->height=old->characterSize;
        font->width=8;
        memcpy(font->data,old->data,st.st_size-4);
        free(loaded_font);
        return font;
    }
    printf("error: no valid font found\n");
    free(loaded_font);
    return NULL;
}

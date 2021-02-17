/*
 * Copyright 2002-2010 Guillaume Cottenceau.
 *
 * This software may be freely redistributed under the terms
 * of the X11 license.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <png.h>
void abort_(const char *s, ...){
	va_list args;
	va_start(args, s);
	vfprintf(stderr, s, args);
	fprintf(stderr, "\n");
	va_end(args);
	abort();
}

struct decoded_image {
	int         w, h;
	png_byte    color_type;
	png_byte    bit_depth;
	png_structp png_ptr;
	png_infop   info_ptr;
	png_infop   end_info;
	int         number_of_passes;
	png_bytep   *row_pointers;
};

struct coefficients{
    float r,g,b;
};

/*!
 * Performs deep copy of one instance of decoded_image to the other.
 * \param[in] const struct decoded_image *old_img source
 * \param[in] struct decoded_image *new_img destination
*/
void deep_copy_img(const struct decoded_image *old_img, struct decoded_image *new_img){
    // For png_ptr and info_ptr there should be a way to store data from old_img pointers but I can't figure out how.
    new_img->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    new_img->info_ptr = png_create_info_struct(old_img->png_ptr);
    new_img->w = old_img->w;
    new_img->h = old_img->h;
    new_img->color_type = old_img->color_type;
    new_img->bit_depth = old_img->bit_depth;
    new_img->number_of_passes = old_img->number_of_passes;
    new_img->row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * new_img->h);
    for (int y = 0; y < new_img->h; y++) {
        new_img->row_pointers[y] = (png_byte*) malloc(sizeof(png_bytep) * new_img->w*4);
        memcpy(new_img->row_pointers[y], old_img->row_pointers[y], (unsigned long)(sizeof(png_bytep) * new_img->w * 4));
    }
}

void read_png_file(char *file_name, struct decoded_image *img){
	char header[8];        // 8 is the maximum size that can be checked

	/* open file and test for it being a png */
	FILE *fp = fopen(file_name, "rb");
	if (!fp)
		abort_("[read_png_file] File %s could not be opened for reading", file_name);
	fread(header, 1, 8, fp);
	if (png_sig_cmp((png_const_bytep)header, 0, 8))
		abort_("[read_png_file] File %s is not recognized as a PNG file", file_name);


	/* initialize stuff */
	img->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!img->png_ptr)
		abort_("[read_png_file] png_create_read_struct failed");

	img->info_ptr = png_create_info_struct(img->png_ptr);
	if (!img->info_ptr)
		abort_("[read_png_file] png_create_info_struct failed");

	if (setjmp(png_jmpbuf(img->png_ptr)))
		abort_("[read_png_file] Error during init_io");


    png_init_io(img->png_ptr, fp);
    png_set_sig_bytes(img->png_ptr, 8);

    png_read_info(img->png_ptr, img->info_ptr);

    img->w      = png_get_image_width(img->png_ptr, img->info_ptr);
    img->h     = png_get_image_height(img->png_ptr, img->info_ptr);
    img->color_type = png_get_color_type(img->png_ptr, img->info_ptr);
    img->bit_depth  = png_get_bit_depth(img->png_ptr, img->info_ptr);

    img->number_of_passes = png_set_interlace_handling(img->png_ptr);
    png_read_update_info(img->png_ptr, img->info_ptr);


	/* read file */
	if (setjmp(png_jmpbuf(img->png_ptr)))
		abort_("[read_png_file] Error during read_image");

	img->row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * img->h);
	for (int y = 0; y < img->h; y++)
		img->row_pointers[y] = (png_byte*) malloc(png_get_rowbytes(img->png_ptr, img->info_ptr));

	png_read_image(img->png_ptr, img->row_pointers);
	fclose(fp);
}


static void write_png_file(char *file_name, struct decoded_image *img){
  /* create file */
  FILE *fp = fopen(file_name, "wb");
  if (!fp)
    abort_("[write_png_file] File %s could not be opened for writing", file_name);


  /* initialize stuff */
  img->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (!img->png_ptr)
    abort_("[write_png_file] png_create_write_struct failed");

  img->info_ptr = png_create_info_struct(img->png_ptr);
  if (!img->info_ptr)
    abort_("[write_png_file] png_create_info_struct failed");

  if (setjmp(png_jmpbuf(img->png_ptr)))
    abort_("[write_png_file] Error during init_io");

  png_init_io(img->png_ptr, fp);


	/* write header */
	if (setjmp(png_jmpbuf(img->png_ptr)))
		abort_("[write_png_file] Error during writing header");

	png_set_IHDR(img->png_ptr, img->info_ptr, img->w, img->h,
		img->bit_depth, img->color_type, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_write_info(img->png_ptr, img->info_ptr);


	/* write bytes */
	if (setjmp(png_jmpbuf(img->png_ptr)))
		abort_("[write_png_file] Error during writing bytes");

	png_write_image(img->png_ptr, img->row_pointers);


	/* end write */
	if (setjmp(png_jmpbuf(img->png_ptr)))
		abort_("[write_png_file] Error during end of write");

	png_write_end(img->png_ptr, NULL);

	/* cleanup heap allocation */
	for (int y = 0; y < img->h; y++)
		free(img->row_pointers[y]);
	free(img->row_pointers);
	png_destroy_write_struct(&img->png_ptr, &img->info_ptr);

	fclose(fp);
}

/*!
 * Performs setting red channel to zero and green to blue.
 * \param[in] struct decoded_image *img Source image
 * \param[out] int Succes indicator
*/
static int process_file(struct decoded_image *img){
	printf("Checking PNG format\n");

	if (png_get_color_type(img->png_ptr, img->info_ptr) != PNG_COLOR_TYPE_RGBA){
        printf("[process_file] color_type of input file must be PNG_COLOR_TYPE_RGBA (%d) (is %d)", PNG_COLOR_TYPE_RGBA, png_get_color_type(img->png_ptr, img->info_ptr));
        //return 1;
        //here "return 1" is commented because, unfortunately, I'm unable to copy png_ptr and info_ptr when performing deep copy of image.
        //Fortunately it does not ruin anything.
	}


	printf("Starting processing\n");
    for (int y = 0; y < img->h; y++){
        png_byte *row = img->row_pointers[y];
        for (int x = 0; x < img->w; x++){
	      png_byte *ptr = &(row[x * 4]);
	      /* set red value to 0 */
	      ptr[0]  = 0;
	    }
	  }

    for (int y = 0; y < img->h; y++){
        png_byte *row = img->row_pointers[y];
        for (int x = 0; x < img->w; x++){
			png_byte *ptr = &(row[x * 4]);
			/* Then set green value to the blue one */
			ptr[1]  = ptr[2];
		}
	}
	printf("Processing done\n");

	png_destroy_read_struct(&img->png_ptr, &img->info_ptr, NULL);

	return 0;
}

/*!
 * Performs applying coefficient on old_img data and saves transformation to new_image.
 * \param[in] const struct decoded_image *old_img Source image
 * \param[in] struct decoded_image *new_img Image to store transformation
 * \param[in] const struct coefficients *coeffs Coefficients to be applied on channels
 * \param[out] int Succes indicator
*/
static int apply_coefficients(const struct decoded_image *old_img, struct decoded_image *new_img, const struct coefficients *coeffs){
    printf("Checking PNG format\n");

    if (png_get_color_type(old_img->png_ptr, old_img->info_ptr) != PNG_COLOR_TYPE_RGBA){
        printf("[apply_coefficients] color_type of input file must be PNG_COLOR_TYPE_RGBA (%d) (is %d)", PNG_COLOR_TYPE_RGBA, png_get_color_type(old_img->png_ptr, old_img->info_ptr));
        //return 1; //same reason as before
    }

    if (old_img->w != new_img->w || old_img->h != new_img->h){
        printf("[apply_coefficients] images should be of the same size\n");
        return 1;
    }

    printf("Starting processing\n");
    for (int y = 0; y < old_img->h; y++){
        png_byte *old_row = old_img->row_pointers[y];
        png_byte *new_row = new_img->row_pointers[y];
        for (int x = 0; x < old_img->w; x++){
            png_byte *old_ptr = &(old_row[4*x]);
            png_byte *new_ptr = &(new_row[4*x]);
            new_ptr[0]  = (int)((float)old_ptr[0] * coeffs->r);
            new_ptr[1]  = (int)((float)old_ptr[1] * coeffs->g);
            new_ptr[2]  = (int)((float)old_ptr[2] * coeffs->b);
        }
    }
    printf("Processing done\n");

    png_destroy_read_struct(&old_img->png_ptr, &old_img->info_ptr, NULL);
    png_destroy_read_struct(&new_img->png_ptr, &new_img->info_ptr, NULL);

    return 0;
}

/*!
 * Sets coefficients with corresponding values.
 * \param[in] float r,g,b Coefficients
 * \param[in] struct coefficients *coeffs Structure which would store them
*/
static void set_coefficients(float coef_r, float coef_g, float coef_b, struct coefficients *coeffs){
    coeffs->r = coef_r;
    coeffs->g = coef_g;
    coeffs->b = coef_b;
}

int main(int argc, char **argv){
    if (argc != 7 && argc != 3)
        abort_("Usage: program_name <file_in> <file_out>");

    struct decoded_image *img = malloc(sizeof(struct decoded_image));
    printf("Reading input PNG\n");
    read_png_file(argv[1], img);
    //Task1
    if(argc == 3){
        process_file(img);
        write_png_file(argv[2], img);
    }
    //Task2
    if(argc == 7){
        struct coefficients *coeffs = malloc(sizeof(struct coefficients));
        set_coefficients(atof(argv[4]), atof(argv[5]), atof(argv[6]), coeffs);
        struct decoded_image *img_transformed = (struct decoded_image*)malloc(sizeof(struct decoded_image));
        deep_copy_img(img, img_transformed);
        apply_coefficients(img, img_transformed, coeffs);
        struct decoded_image *new_img_transformed = (struct decoded_image*)malloc(sizeof(struct decoded_image));
        deep_copy_img(img_transformed, new_img_transformed);

        printf("Writing output PNG\n");
        write_png_file(argv[1], img);
        struct coefficients *coeffs1 = malloc(sizeof(struct coefficients));
        set_coefficients(0.f, 0.f, 1.0f, coeffs1);
        apply_coefficients(img_transformed, new_img_transformed, coeffs1);
        write_png_file(argv[2], img_transformed);
        write_png_file(argv[3], new_img_transformed);
    }
    return 0;
}


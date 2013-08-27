#include <stdlib.h>
#include <string.h>

#include "blk.h"
#include "rabin.h"
#include "rabin_win.h"
#include "log.h"

static struct blk *blk=NULL;
static char *gcp=NULL;
static char *gbuf=NULL;
static char *gbuf_end=NULL;

static int first=0;

// This is where the magic happens.
// Return -1 for error, 1 for got a block, 0 for no block got.
static int blk_read(struct rconf *rconf, struct win *win, struct sbuf *sb, struct blist *blist)
{
	char c;

	if(!blk && !(blk=blk_alloc_with_data(rconf->blk_max)))
		return -1;

	for(; gcp<gbuf_end; gcp++)
	{
		c=*gcp;

		blk->fingerprint = (blk->fingerprint * rconf->prime) + c;
		win->checksum    = (win->checksum    * rconf->prime) + c
				   - (win->data[win->pos] * rconf->multiplier);
		win->data[win->pos] = c;

		win->pos++;
		win->total_bytes++;
		blk->data[blk->length++] = c;

		if(win->pos == rconf->win) win->pos=0;

		if( blk->length >= rconf->blk_min
		 && (blk->length == rconf->blk_max
		  || (win->checksum % rconf->blk_avg) == rconf->prime))
		{
			if(first)
			{
				sb->bstart=blk;
				first=0;
			}
			if(!sb->bsighead)
			{
printf("SET A: %s\n", sb->path);
				sb->bsighead=blk;
			}
			blk_add_to_list(blk, blist);
			blk=NULL;

			gcp++;
			return 1;
		}
	}
	return 0;
}

int blks_generate(struct config *conf, struct sbuf *sb, struct blist *blist, struct win *win)
{
	static int r;
	static ssize_t bytes;

	if(sb->bfd.mode==BF_CLOSED)
	{
		if(sbuf_open_file(sb, conf)) return -1;
		first=1;
	}

	if(!gbuf)
	{
		if(!(gbuf=(char *)malloc(conf->rconf.blk_max)))
		{
			log_out_of_memory(__FUNCTION__);
			sbuf_close_file(sb);
			return -1;
		}
		gbuf_end=gbuf;
		gcp=gbuf;
	}

	if(gcp<gbuf_end)
	{
		// Could have got a fill before buf ran out -
		// need to resume from the same place in that case.
		if((r=blk_read(&conf->rconf, win, sb, blist)))
		{
			if(r<0)
			{
				// Error
				sbuf_close_file(sb);
				return -1;
			}
			// Got a block.
			return 0;
		}
		// Did not get a block. Carry on and read more.
	}
	while((bytes=sbuf_read(sb, gbuf, conf->rconf.blk_max)))
	{
		gcp=gbuf;
		gbuf_end=gbuf+bytes;
		sb->bytes_read+=bytes;
		if((r=blk_read(&conf->rconf, win, sb, blist)))
		{
			if(r<0) return -1; // Error.
			return 0; // Got a block
		}
		// Did not get a block. Maybe should try again?
		// If there are async timeouts, look at this!
		return 0;
	}

	// Getting here means there is no more to read from the file.
	// Make sure to deal with anything left over.
	if(blk)
	{
		if(blk->length)
		{
			if(first)
			{
				sb->bstart=blk;
				first=0;
			}
			if(!sb->bsighead)
			{
				sb->bsighead=blk;
			}
			blk_add_to_list(blk, blist);
		}
		else blk_free(blk);
		blk=NULL;
	}
	else if(!sb->bytes_read)
	{
		// Empty file, set up an empty block so that the server
		// can skip over it.
		if(!(blk=blk_alloc())) return -1;
		sb->bstart=blk;
		sb->bsighead=blk;
		blk_add_to_list(blk, blist);
		blk=NULL;
	}
	if(blist->tail) sb->bend=blist->tail;
	sbuf_close_file(sb);
	return 0;
}
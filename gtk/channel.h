#ifndef CHANNEL_H
#define CHANNEL_H

#include <gtk/gtk.h>

typedef enum {
	CHANNEL_TYPE_INPUT,
	CHANNEL_TYPE_PLAYBACK,
	CHANNEL_TYPE_OUTPUT,
} ChannelType;

typedef enum {
	CHANNEL_FLAG_ANALOG = 1,
	CHANNEL_FLAG_MIC = 2,
	CHANNEL_FLAG_INSTRUMENT = 4,
} ChannelFlags;

G_DECLARE_FINAL_TYPE(Channel, channel, OSCMIX, CHANNEL, GtkBox)
GType channel_type_get_type(void);
GType channel_flags_get_type(void);

int channel_get_id(Channel *);
const char *channel_get_name(Channel *);
void channel_set_output_iter(Channel *self, GtkTreeIter *iter);

#endif

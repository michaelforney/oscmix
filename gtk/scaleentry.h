#ifndef SCALE_ENTRY_H
#define SCALE_ENTRY_H

#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE(ScaleEntry, scale_entry, OSCMIX, SCALE_ENTRY, GtkBox)

GtkAdjustment *scale_entry_get_adjustment(ScaleEntry *);

#endif

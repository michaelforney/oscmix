#ifndef EQPLOT_H
#define EQPLOT_H

#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE(EQPlot, eq_plot, OSCMIX, EQ_PLOT, GtkDrawingArea)

typedef enum {
	EQ_FILTER_PEAK,
	EQ_FILTER_LOWSHELF,
	EQ_FILTER_HIGHSHELF,
	EQ_FILTER_LOWPASS,
	EQ_FILTER_HIGHPASS,
} EQFilterType;

void eq_plot_set_n_bands(EQPlot *self, int n);
void eq_plot_set_band_type(EQPlot *self, int index, EQFilterType type);
void eq_plot_set_band_freq(EQPlot *self, int index, int freq);
void eq_plot_set_band_gain(EQPlot *self, int index, double gain);
void eq_plot_set_band_q(EQPlot *self, int index, double q);
void eq_plot_set_lowcut_order(EQPlot *self, int order);
void eq_plot_set_lowcut_freq(EQPlot *self, int freq);

#endif

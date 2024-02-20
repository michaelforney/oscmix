#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include "eqplot.h"

struct _EQPlot {
	GtkDrawingArea base;
	GArray *bands;
	int lowcut_freq;
	int lowcut_order;
};

typedef struct {
	EQFilterType type;
	double freq;
	double gain;
	double q;

	/*
	           (a0 + a1 * f² + a2 * f⁴)
	|H(if)|² = ────────────────────────
	           (b0 + b1 * f² +      f⁴)
	*/
	double a0, a1, a2, b0, b1;
} EQBand;

G_DEFINE_TYPE(EQPlot, eq_plot, GTK_TYPE_DRAWING_AREA)

enum {
	PROP_N_BANDS = 1,
	PROP_LOWCUT_FREQ,
};

static GType
eq_filter_type_get_type(void)
{
	static gsize once;
	static const GEnumValue values[] = {
		{EQ_FILTER_PEAK, "EQ_FILTER_PEAK", "peak"},
		{EQ_FILTER_LOWSHELF, "EQ_FILTER_LOWSHELF", "lowshelf"},
		{EQ_FILTER_HIGHSHELF, "EQ_FILTER_HIGHSHELF", "highshelf"},
		{EQ_FILTER_LOWPASS, "EQ_FILTER_LOWPASS", "lowpass"},
		{EQ_FILTER_HIGHPASS, "EQ_FILTER_HIGHPASS", "highpass"},
		{0},
	};
	GType type;

	if (g_once_init_enter(&once)) {
		type = g_enum_register_static("EQFilterType", values);
		g_once_init_leave(&once, type);
	}
	return once;
}

static void
eq_plot_set_property(GObject *obj, guint id, const GValue *val, GParamSpec *spec)
{
	EQPlot *self;

	self = OSCMIX_EQ_PLOT(obj);
	switch (id) {
	case PROP_N_BANDS:
		eq_plot_set_n_bands(self, g_value_get_uint(val));
		break;
	case PROP_LOWCUT_FREQ:
		eq_plot_set_lowcut_freq(self, g_value_get_uint(val));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
		break;
	}
}

static void
eq_plot_get_property(GObject *obj, guint id, GValue *val, GParamSpec *spec)
{
	EQPlot *self;

	self = OSCMIX_EQ_PLOT(obj);
	switch (id) {
	case PROP_N_BANDS:
		g_value_set_uint(val, self->bands->len);
		break;
	case PROP_LOWCUT_FREQ:
		g_value_set_uint(val, self->lowcut_freq);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
		break;
	}
}

static gboolean
eq_plot_draw(GtkWidget *widget, cairo_t *cr)
{
	EQPlot *self;
	EQBand *band;
	GtkStyleContext *style;
	GdkRGBA color;
	guint x, width, height;
	double y, f2, f4;
	int i;
	PangoLayout *layout;
	PangoRectangle rect;
	const char *const labels[] = {"100", "1k", "10k"};

	self = OSCMIX_EQ_PLOT(widget);
	style = gtk_widget_get_style_context(widget);
	width = gtk_widget_get_allocated_width(widget);
	height = gtk_widget_get_allocated_height(widget);

	/* background */
	gtk_render_background(style, cr, 0, 0, width, height);

	/* grid lines */
	for (i = 0; i < 5; ++i) {
		y = height * (4 + i * 10) / 48 + 0.5;
		cairo_move_to(cr, 0.5, y);
		cairo_line_to(cr, width + 0.5, y);
	}
	for (i = 0; i < 3; ++i) {
		x = (7 + 10 * i) * width / 30;
		cairo_move_to(cr, x + 0.5, 0.5);
		cairo_line_to(cr, x + 0.5, height + 0.5);
	}
	if (gtk_style_context_lookup_color(style, "borders", &color))
		gdk_cairo_set_source_rgba(cr, &color);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	/* EQ curve */
	cairo_save(cr);
	cairo_translate(cr, 0.5, height / 2. + 0.5);
	cairo_scale(cr, 1, height / 48.);
	for (x = 0; x < width; ++x) {
		f2 = pow(10, 2 * (x * 3. / width + 1.3));
		f4 = f2 * f2;
		y = 1;
		for (i = 0; i < self->bands->len; ++i) {
			band = &g_array_index(self->bands, EQBand, i);
			y *= (band->a0 + band->a1 * f2 + band->a2 * f4) / (band->b0 + band->b1 * f2 + f4);
		}
		if (self->lowcut_order) {
			static const double kn[] = {1, 0.655, 0.528, 0.457};
			double lc, k;

			k = kn[self->lowcut_order - 1];
			lc = f2 / (f2 + k * k * self->lowcut_freq * self->lowcut_freq);
			for (i = 0; i < self->lowcut_order; ++i) {
				y = y * lc;
			}
		}
		y = -10 * log10(y);
		cairo_line_to(cr, x, y);
	}
	cairo_restore(cr);
	cairo_set_line_width(cr, 2);
	if (gtk_style_context_lookup_color(style, "theme_selected_bg_color", &color))
		gdk_cairo_set_source_rgba(cr, &color);
	cairo_stroke(cr);

	/* axis labels */
	for (i = 0; i < G_N_ELEMENTS(labels); ++i) {
		x = (7 + 10 * i) * width / 30;
		layout = gtk_widget_create_pango_layout(widget, labels[i]);
		pango_layout_get_pixel_extents(layout, NULL, &rect);
		rect.x = x - rect.width / 2;
		rect.y = height - rect.height;
		gtk_render_background(style, cr, rect.x, rect.y, rect.width, rect.height);
		gtk_render_layout(style, cr, rect.x, rect.y, layout);
		g_object_unref(layout);
	}

	/* border */
	gtk_render_frame(style, cr, 0, 0, width, height);

	return false;
}

static void
eq_plot_class_init(EQPlotClass *class)
{
	g_type_ensure(eq_filter_type_get_type());
	G_OBJECT_CLASS(class)->set_property = eq_plot_set_property;
	G_OBJECT_CLASS(class)->get_property = eq_plot_get_property;
	GTK_WIDGET_CLASS(class)->draw = eq_plot_draw;
	gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(class), "eqplot");
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_N_BANDS,
		g_param_spec_uint("n-bands", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_LOWCUT_FREQ,
		g_param_spec_uint("lowcut-freq", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READWRITE));
}

static void
eq_plot_init(EQPlot *self)
{
	gtk_widget_set_size_request(GTK_WIDGET(self), -1, 100);
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "view");
	self->bands = g_array_new(false, true, sizeof(EQBand));
}

static void
update_band_coeff(EQBand *band)
{
	double A, Q, f2, f4;

	f2 = band->freq * band->freq;
	f4 = f2 * f2;
	A = pow(10, band->gain / 40.);
	Q = 1 / (band->q * band->q);  /* 1/Q^2 */
	switch (band->type) {
	case EQ_FILTER_PEAK:
		band->a0 = f4;
		band->a1 = (Q * A * A - 2) * f2;
		band->a2 = 1;
		band->b0 = f4;
		band->b1 = (Q / (A * A) - 2) * f2;
		break;
	case EQ_FILTER_LOWPASS:
		band->a0 = f4;
		band->a1 = 0;
		band->a2 = 0;
		band->b0 = f4;
		band->b1 = (Q - 2) * f2;
		break;
	case EQ_FILTER_HIGHPASS:
		band->a0 = 0;
		band->a1 = 0;
		band->a2 = 1;
		band->b0 = f4;
		band->b1 = (Q - 2) * f2;
		break;
	case EQ_FILTER_LOWSHELF:
		band->a0 = A * A * f4;
		band->a1 = A * (Q - 2) * f2;
		band->a2 = 1;
		band->b0 = f4 / (A * A);
		band->b1 = (Q - 2) / A * f2;
		break;
	case EQ_FILTER_HIGHSHELF:
		band->a0 = A * A * f4;
		band->a1 = A * A * A * (Q - 2) * f2;
		band->a2 = A * A * A * A;
		band->b0 = A * A * f4;
		band->b1 = A * (Q - 2) * f2;
		break;
	}
}

void
eq_plot_set_n_bands(EQPlot *self, int n)
{
	g_array_set_size(self->bands, n);
	gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
eq_plot_set_band_type(EQPlot *self, int index, EQFilterType type)
{
	EQBand *band;

	if (index >= 0 && index < self->bands->len) {
		band = &g_array_index(self->bands, EQBand, index);
		band->type = type;
		update_band_coeff(band);
		gtk_widget_queue_draw(GTK_WIDGET(self));
	}
}

void
eq_plot_set_band_freq(EQPlot *self, int index, int freq)
{
	EQBand *band;

	if (index >= 0 && index < self->bands->len) {
		band = &g_array_index(self->bands, EQBand, index);
		band->freq = freq;
		update_band_coeff(band);
		gtk_widget_queue_draw(GTK_WIDGET(self));
	}
}

void
eq_plot_set_band_gain(EQPlot *self, int index, double gain)
{
	EQBand *band;

	if (index >= 0 && index < self->bands->len) {
		band = &g_array_index(self->bands, EQBand, index);
		band->gain = gain;
		update_band_coeff(band);
		gtk_widget_queue_draw(GTK_WIDGET(self));
	}
}

void
eq_plot_set_band_q(EQPlot *self, int index, double q)
{
	EQBand *band;

	if (index >= 0 && index < self->bands->len) {
		band = &g_array_index(self->bands, EQBand, index);
		band->q = q;
		update_band_coeff(band);
		gtk_widget_queue_draw(GTK_WIDGET(self));
	}
}

void
eq_plot_set_lowcut_order(EQPlot *self, int order)
{
	assert(order >= 0 && order <= 4);
	self->lowcut_order = order;
	gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
eq_plot_set_lowcut_freq(EQPlot *self, int freq)
{
	self->lowcut_freq = freq;
	gtk_widget_queue_draw(GTK_WIDGET(self));
}

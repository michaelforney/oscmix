#include <assert.h>
#include <stdbool.h>
#include "channel.h"
#include "eqplot.h"
#include "mixer.h"

struct _Channel {
	GtkBox box;
	ChannelType type;
	ChannelFlags flags;
	Mixer *osc;
	const char *name;
	int id;
	Channel *right;
	GtkWidget *stack_buttons;
	GtkWidget *stack_hide;
	GtkWidget *last_toggled;
	GQuark quark;

	struct {
		GtkWidget *name;
		GtkAdjustment *gain;
		GtkWidget *gain_button;
		GtkWidget *stereo;
		GtkWidget *stereowidth;
		GtkWidget *msproc;
		GtkToggleButton *record;
		GtkWidget *instr;
		GtkWidget *mic48v;
		GtkWidget *level;
		GtkWidget *level_right;
		GtkAdjustment *volume;
		GtkWidget *volume_scale;
		GtkWidget *mute;
		GtkWidget *phase;
		GtkWidget *phase_right;
		GtkWidget *fx;
		GtkWidget *fx_label;
		GtkWidget *autoset;
		GtkWidget *autolevel;
		GtkWidget *eq;
		GtkWidget *eq_box;
		GtkWidget *lowcut;
		GtkWidget *dynamics;
		GtkWidget *dynamics_box;
		GtkWidget *reflevel;
		GtkWidget *output;
		GtkWidget *play_record;
	} ui;
	GtkAdjustment *pan;
	EQPlot *eq_plot;
	GtkWidget *eq_band1type;
	GtkAdjustment *eq_band1gain;
	GtkAdjustment *eq_band1freq;
	GtkAdjustment *eq_band1q;
	GtkAdjustment *eq_band2gain;
	GtkAdjustment *eq_band2freq;
	GtkAdjustment *eq_band2q;
	GtkWidget *eq_band3type;
	GtkAdjustment *eq_band3gain;
	GtkAdjustment *eq_band3freq;
	GtkAdjustment *eq_band3q;
	GtkWidget *lowcut;
	GtkWidget *lowcut_slope;
	GtkAdjustment *lowcut_freq;
};

static unsigned output_changed_signal;

G_DEFINE_TYPE(Channel, channel, GTK_TYPE_BOX)

void bind_osc(char *addr, GType type, gpointer ptr, const char *prop);

GType
channel_type_get_type(void)
{
	static gsize once;
	static const GEnumValue values[] = {
		{CHANNEL_TYPE_INPUT, "CHANNEL_TYPE_INPUT", "input"},
		{CHANNEL_TYPE_PLAYBACK, "CHANNEL_TYPE_PLAYBACK", "playback"},
		{CHANNEL_TYPE_OUTPUT, "CHANNEL_TYPE_OUTPUT", "output"},
		{0},
	};
	GType type;

	if (g_once_init_enter(&once)) {
		type = g_enum_register_static("ChannelType", values);
		g_once_init_leave(&once, type);
	}
	return once;
}

GType
channel_flags_get_type(void)
{
	static gsize once;
	static const GFlagsValue values[] = {
		{CHANNEL_FLAG_ANALOG, "CHANNEL_FLAG_ANALOG", "analog"},
		{CHANNEL_FLAG_MIC, "CHANNEL_FLAG_MIC", "mic"},
		{CHANNEL_FLAG_INSTRUMENT, "CHANNEL_FLAG_INSTRUMENT", "instrument"},
		{0},
	};
	GType type;

	if (g_once_init_enter(&once)) {
		type = g_flags_register_static("ChannelFlags", values);
		g_once_init_leave(&once, type);
	}
	return once;
}

static void
osc_levels(GValue *arg, guint len, gpointer ptr)
{
	Channel *channel;
	GtkLevelBar *bar;
	float value;

	if (len == 0)
		return;
	channel = OSCMIX_CHANNEL(ptr);
	bar = GTK_LEVEL_BAR(channel->ui.level);
	value = CLAMP(g_value_get_float(&arg[0]) + 65, gtk_level_bar_get_min_value(bar), gtk_level_bar_get_max_value(bar));
	gtk_level_bar_set_value(bar, value);
}

static void
channel_constructed(GObject *obj)
{
	Channel *self;
	GEnumValue *type;
	Mixer *osc;
	char *prefix;

	self = OSCMIX_CHANNEL(obj);
	osc = self->osc;
	type = g_enum_get_value(g_type_class_peek_static(channel_type_get_type()), self->type);
	prefix = g_strdup_printf("/%s/%d", type->value_nick, self->id);
	self->quark = g_quark_from_string(prefix);
	gtk_label_set_text(GTK_LABEL(self->ui.name), self->name);
	mixer_bind(osc, g_strdup_printf("%s/mute", prefix), G_TYPE_BOOLEAN, self->ui.mute, "active");
	mixer_bind(osc, g_strdup_printf("%s/stereo", prefix), G_TYPE_BOOLEAN, self->ui.stereo, "active");
	mixer_bind(osc, g_strdup_printf("/%s/%d/record", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.record, "active");
	//bind_osc(g_strdup_printf("/%s/%d/playchan", type->value_nick, self->id), G_TYPE_INT, self->ui.stereo, "active");
	mixer_bind(osc, g_strdup_printf("/%s/%d/msproc", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.msproc, "active");
	mixer_bind(osc, g_strdup_printf("/%s/%d/phase", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.phase, "active");
	mixer_bind(osc, g_strdup_printf("/%s/%d/fx", type->value_nick, self->id), G_TYPE_FLOAT, self->ui.fx, "value");
	switch (self->type) {
	case CHANNEL_TYPE_INPUT:
		if (self->flags & CHANNEL_FLAG_ANALOG) {
			gtk_widget_show(self->ui.gain_button);
			mixer_bind(osc, g_strdup_printf("/%s/%d/gain", type->value_nick, self->id), G_TYPE_FLOAT, self->ui.gain, "value");
			gtk_widget_show(self->ui.autoset);
			mixer_bind(osc, g_strdup_printf("/%s/%d/autoset", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.autoset, "active");
		}
		if (self->flags & CHANNEL_FLAG_INSTRUMENT) {
			gtk_widget_show(self->ui.instr);
			mixer_bind(osc, g_strdup_printf("/%s/%d/hi-z", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.instr, "active");
		}
		break;
	case CHANNEL_TYPE_PLAYBACK:
		gtk_widget_destroy(GTK_WIDGET(self->ui.eq_box));
		gtk_widget_destroy(GTK_WIDGET(self->ui.dynamics_box));
		break;
	case CHANNEL_TYPE_OUTPUT:
		mixer_bind(osc, g_strdup_printf("/output/%d/balance", self->id), G_TYPE_INT, self->pan, "value");
		mixer_bind(osc, g_strdup_printf("/output/%d/volume", self->id), G_TYPE_FLOAT, self->ui.volume, "value");
		gtk_label_set_text(GTK_LABEL(self->ui.fx_label), "FX Return");
		gtk_widget_destroy(self->ui.output);
		self->ui.output = NULL;
		break;
	}
	if (self->flags & CHANNEL_FLAG_ANALOG) {
		if (self->flags & CHANNEL_FLAG_MIC) {
			gtk_widget_show(self->ui.mic48v);
			mixer_bind(osc, g_strdup_printf("/%s/%d/48v", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.mic48v, "active");
		} else {
			gtk_widget_show(self->ui.reflevel);
			mixer_bind(osc, g_strdup_printf("/%s/%d/reflevel", type->value_nick, self->id), G_TYPE_INT, self->ui.reflevel, "active");
		}
	}
	if (self->type != CHANNEL_TYPE_PLAYBACK) {
		mixer_bind(osc, g_strdup_printf("/%s/%d/eq", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.eq, "active");
		mixer_bind(osc, g_strdup_printf("%s/eq/band1type", prefix), G_TYPE_INT, self->eq_band1type, "active");
		mixer_bind(osc, g_strdup_printf("%s/eq/band1gain", prefix), G_TYPE_FLOAT, self->eq_band1gain, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band1freq", prefix), G_TYPE_INT, self->eq_band1freq, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band1q", prefix), G_TYPE_FLOAT, self->eq_band1q, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band2gain", prefix), G_TYPE_FLOAT, self->eq_band2gain, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band2freq", prefix), G_TYPE_INT, self->eq_band2freq, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band2q", prefix), G_TYPE_FLOAT, self->eq_band2q, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band3type", prefix), G_TYPE_INT, self->eq_band3type, "active");
		mixer_bind(osc, g_strdup_printf("%s/eq/band3gain", prefix), G_TYPE_FLOAT, self->eq_band3gain, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band3freq", prefix), G_TYPE_INT, self->eq_band3freq, "value");
		mixer_bind(osc, g_strdup_printf("%s/eq/band3q", prefix), G_TYPE_FLOAT, self->eq_band3q, "value");
		mixer_bind(osc, g_strdup_printf("%s/lowcut", prefix), G_TYPE_BOOLEAN, self->lowcut, "active");
		mixer_bind(osc, g_strdup_printf("%s/lowcut/freq", prefix), G_TYPE_INT, self->lowcut_freq, "value");
		mixer_bind(osc, g_strdup_printf("%s/lowcut/slope", prefix), G_TYPE_INT, self->lowcut_slope, "active");
		mixer_bind(osc, g_strdup_printf("/%s/%d/dynamics", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.dynamics, "active");
		mixer_bind(osc, g_strdup_printf("/%s/%d/autolevel", type->value_nick, self->id), G_TYPE_BOOLEAN, self->ui.autolevel, "active");
	}
	mixer_connect(osc, g_strdup_printf("/%s/%d/level", type->value_nick, self->id), osc_levels, self);
}

enum {
	PROP_TYPE = 1,
	PROP_FLAGS,
	PROP_ID,
	PROP_MIXER,
	PROP_NAME,
	PROP_RIGHT,
	PROP_RECORDVIEW,
	PROP_OUTPUTS_MODEL,
};

static void
channel_set_property(GObject *obj, guint id, const GValue *val, GParamSpec *spec)
{
	Channel *self;
	GtkTreeModel *model;
	GtkTreeIter iter;

	self = OSCMIX_CHANNEL(obj);
	switch (id) {
	case PROP_TYPE:
		self->type = g_value_get_enum(val);
		break;
	case PROP_FLAGS:
		self->flags = g_value_get_flags(val);
		break;
	case PROP_ID:
		self->id = g_value_get_int(val);
		break;
	case PROP_MIXER:
		self->osc = OSCMIX_MIXER(g_value_get_object(val));
		break;
	case PROP_NAME:
		self->name = g_value_get_string(val);
		break;
	case PROP_RIGHT:
		self->right = OSCMIX_CHANNEL(g_value_get_object(val));
		if (self->right) {
			g_object_bind_property(self->ui.stereo, "active", self->right->ui.stereo, "active", G_BINDING_BIDIRECTIONAL);
			g_object_bind_property(self->right->ui.level, "value", self->ui.level_right, "value", G_BINDING_DEFAULT);
			g_object_bind_property(self->right->ui.phase, "active", self->ui.phase_right, "active", G_BINDING_BIDIRECTIONAL);
		}
		break;
	case PROP_RECORDVIEW:
		gtk_widget_set_visible(self->ui.play_record, g_value_get_boolean(val));
		break;
	case PROP_OUTPUTS_MODEL:
		if (!self->ui.output)
			break;
		model = g_value_get_object(val);
		gtk_combo_box_set_model(GTK_COMBO_BOX(self->ui.output), model);
		if (model && gtk_tree_model_get_iter_first(model, &iter)) {
			Channel *out;
			GtkAdjustment *adj;
			GEnumValue *type;
			bool first;

			first = true;
			do {
				gtk_tree_model_get(model, &iter, 0, &out, -1);
				adj = g_object_ref_sink(gtk_adjustment_new(-65, -65, 6, 0.5, 0, 0));
				g_object_set_qdata(G_OBJECT(self), out->quark, adj);
				type = g_enum_get_value(g_type_class_peek_static(channel_type_get_type()), self->type);
				mixer_bind(self->osc, g_strdup_printf("/mix/%d/%s/%d", out->id, type->value_nick, self->id), G_TYPE_FLOAT, adj, "value");
				if (first) {
					gtk_combo_box_set_active_iter(GTK_COMBO_BOX(self->ui.output), &iter);
					first = false;
				}
			} while (gtk_tree_model_iter_next(model, &iter));
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
		break;
	}
}

static void
channel_get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec)
{
	Channel *self;

	self = OSCMIX_CHANNEL(obj);
	switch (id) {
	case PROP_TYPE:
		g_value_set_enum(val, self->type);
		break;
	case PROP_FLAGS:
		g_value_set_flags(val, self->flags);
		break;
	case PROP_ID:
		g_value_set_int(val, self->id);
		break;
	case PROP_MIXER:
		g_value_set_object(val, G_OBJECT(self->osc));
		break;
	case PROP_NAME:
		g_value_set_string(val, self->name);
		break;
	case PROP_RIGHT:
		g_value_set_object(val, G_OBJECT(self->right));
		break;
	case PROP_RECORDVIEW:
		g_value_set_boolean(val, gtk_widget_get_visible(self->ui.play_record));
		break;
	case PROP_OUTPUTS_MODEL:
		g_value_set_object(val, gtk_combo_box_get_model(GTK_COMBO_BOX(self->ui.output)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
		break;
	}
}

static void
channel_dispose(GObject *obj)
{
	Channel *self;

	self = OSCMIX_CHANNEL(obj);
	mixer_disconnect_by_data(self->osc, self);
}

static void
on_stereo_toggled(GtkToggleButton *button, gpointer ptr)
{
	Channel *self;
	char *name;
	bool active;

	self = OSCMIX_CHANNEL(ptr);
	if (!self->right)
		return;
	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->ui.stereo));
	gtk_widget_set_visible(GTK_WIDGET(self->right), !active);
	name = NULL;
	if (active) {
		const char *l, *r;

		for (l = self->name, r = self->right->name; *l && *l == *r; ++l, ++r)
			;
		if (*r)
			name = g_strjoin("/", self->name, r, NULL);
	}
	gtk_label_set_text(GTK_LABEL(self->ui.name), name ? name : self->name);
	g_free(name);
}

static gboolean
on_volume_output(GtkSpinButton *button, gpointer ptr)
{
	GtkAdjustment *adj;
	double val;

	adj = gtk_spin_button_get_adjustment(button);
	val = gtk_adjustment_get_value(adj);
	if (val == -65) {
		gtk_entry_set_text(GTK_ENTRY(button), "-inf");
		return true;
	}
	return false;
}

static void
on_eq_bandtype_changed(GtkComboBox *combo, gpointer ptr, int band)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	EQFilterType type;

	if (gtk_combo_box_get_active_iter(combo, &iter)) {
		model = gtk_combo_box_get_model(combo);
		gtk_tree_model_get(model, &iter, 0, &type, -1);
		eq_plot_set_band_type(OSCMIX_CHANNEL(ptr)->eq_plot, band, type);
	}
}

static void on_eq_band1type_changed(GtkComboBox *combo, gpointer ptr) { on_eq_bandtype_changed(combo, ptr, 0); }
static void on_eq_band3type_changed(GtkComboBox *combo, gpointer ptr) { on_eq_bandtype_changed(combo, ptr, 2); }

static void
on_eq_bandgain_changed(GtkAdjustment *adj, gpointer ptr, int band)
{
	eq_plot_set_band_gain(OSCMIX_CHANNEL(ptr)->eq_plot, band, gtk_adjustment_get_value(adj));
}

static void on_eq_band1gain_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandgain_changed(adj, ptr, 0); }
static void on_eq_band2gain_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandgain_changed(adj, ptr, 1); }
static void on_eq_band3gain_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandgain_changed(adj, ptr, 2); }

static void
on_eq_bandfreq_changed(GtkAdjustment *adj, gpointer ptr, int band)
{
	eq_plot_set_band_freq(OSCMIX_CHANNEL(ptr)->eq_plot, band, gtk_adjustment_get_value(adj));
}

static void on_eq_band1freq_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandfreq_changed(adj, ptr, 0); }
static void on_eq_band2freq_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandfreq_changed(adj, ptr, 1); }
static void on_eq_band3freq_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandfreq_changed(adj, ptr, 2); }

static void
on_eq_bandq_changed(GtkAdjustment *adj, gpointer ptr, int band)
{
	eq_plot_set_band_q(OSCMIX_CHANNEL(ptr)->eq_plot, band, gtk_adjustment_get_value(adj));
}

static void on_eq_band1q_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandq_changed(adj, ptr, 0); }
static void on_eq_band2q_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandq_changed(adj, ptr, 1); }
static void on_eq_band3q_changed(GtkAdjustment *adj, gpointer ptr) { on_eq_bandq_changed(adj, ptr, 2); }

static void
on_lowcut_changed(GObject *obj, GParamSpec *pspec, gpointer ptr)
{
	Channel *self;
	int order;

	self = OSCMIX_CHANNEL(ptr);
	if (gtk_switch_get_active(GTK_SWITCH(self->lowcut))) {
		order = gtk_combo_box_get_active(GTK_COMBO_BOX(self->lowcut_slope)) + 1;
	} else {
		order = 0;
	}
	eq_plot_set_lowcut_order(self->eq_plot, order);
}

static void
on_output_changed(GtkComboBox *combo, gpointer ptr)
{
	Channel *self, *output;
	GtkAdjustment *adj;
	GtkTreeIter iter;

	self = OSCMIX_CHANNEL(ptr);
	if (gtk_combo_box_get_active_iter(combo, &iter)) {
		gtk_tree_model_get(gtk_combo_box_get_model(combo), &iter, 0, (gpointer)&output, -1);
		adj = g_object_get_qdata(G_OBJECT(self), output->quark);
		assert(adj);
		gtk_range_set_adjustment(GTK_RANGE(self->ui.volume_scale), adj);
		g_signal_emit(self, output_changed_signal, 0, &iter);
	}
}

static void
channel_class_init(ChannelClass *class)
{
	g_type_ensure(eq_plot_get_type());
	G_OBJECT_CLASS(class)->constructed = channel_constructed;
	G_OBJECT_CLASS(class)->set_property = channel_set_property;
	G_OBJECT_CLASS(class)->get_property = channel_get_property;
	G_OBJECT_CLASS(class)->dispose = channel_dispose;
	gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(class), "/oscmix/channel.ui");
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, stack_buttons);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, stack_hide);
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "name", false, G_STRUCT_OFFSET(Channel, ui.name));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "record", false, G_STRUCT_OFFSET(Channel, ui.record));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "level", false, G_STRUCT_OFFSET(Channel, ui.level));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "level_right", false, G_STRUCT_OFFSET(Channel, ui.level_right));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "stereo", false, G_STRUCT_OFFSET(Channel, ui.stereo));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "gain", false, G_STRUCT_OFFSET(Channel, ui.gain));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "gain_button", false, G_STRUCT_OFFSET(Channel, ui.gain_button));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "phase", false, G_STRUCT_OFFSET(Channel, ui.phase));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "phase_right", false, G_STRUCT_OFFSET(Channel, ui.phase_right));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "stereowidth", false, G_STRUCT_OFFSET(Channel, ui.stereowidth));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "msproc", false, G_STRUCT_OFFSET(Channel, ui.msproc));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "instr", false, G_STRUCT_OFFSET(Channel, ui.instr));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "48v", false, G_STRUCT_OFFSET(Channel, ui.mic48v));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "volume", false, G_STRUCT_OFFSET(Channel, ui.volume));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "volume_scale", false, G_STRUCT_OFFSET(Channel, ui.volume_scale));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "mute", false, G_STRUCT_OFFSET(Channel, ui.mute));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "autoset", false, G_STRUCT_OFFSET(Channel, ui.autoset));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "autolevel", false, G_STRUCT_OFFSET(Channel, ui.autolevel));
	//gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "lowcut", false, G_STRUCT_OFFSET(Channel, ui.lowcut));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "dynamics", false, G_STRUCT_OFFSET(Channel, ui.dynamics));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "dynamics_box", false, G_STRUCT_OFFSET(Channel, ui.dynamics_box));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "fx", false, G_STRUCT_OFFSET(Channel, ui.fx));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "fx_label", false, G_STRUCT_OFFSET(Channel, ui.fx_label));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "reflevel", false, G_STRUCT_OFFSET(Channel, ui.reflevel));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "eq", false, G_STRUCT_OFFSET(Channel, ui.eq));
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, pan);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_plot);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band1type);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band1gain);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band1freq);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band1q);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band2gain);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band2freq);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band2q);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band3type);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band3gain);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band3freq);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, eq_band3q);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, lowcut);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, lowcut_slope);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), Channel, lowcut_freq);
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "eq_box", false, G_STRUCT_OFFSET(Channel, ui.eq_box));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "record", false, G_STRUCT_OFFSET(Channel, ui.record));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "output", false, G_STRUCT_OFFSET(Channel, ui.output));
	gtk_widget_class_bind_template_child_full(GTK_WIDGET_CLASS(class), "play_record", false, G_STRUCT_OFFSET(Channel, ui.play_record));
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_stereo_toggled);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_volume_output);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band1type_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band3type_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band1gain_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band2gain_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band3gain_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band1freq_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band2freq_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band3freq_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band1q_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band2q_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_eq_band3q_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_lowcut_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_output_changed);

	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_TYPE,
		g_param_spec_enum("type", NULL, NULL, channel_type_get_type(), CHANNEL_TYPE_INPUT, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_FLAGS,
		g_param_spec_flags("flags", NULL, NULL, channel_flags_get_type(), 0, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_ID,
		g_param_spec_int("id", NULL, NULL, 0, 20, 0, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_MIXER,
		g_param_spec_object("mixer", NULL, NULL, mixer_get_type(), G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_NAME,
		g_param_spec_string("name", NULL, NULL, NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_RIGHT,
		g_param_spec_object("right", NULL, NULL, channel_get_type(), G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_RECORDVIEW,
		g_param_spec_boolean("record-view", NULL, NULL, false, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_OUTPUTS_MODEL,
		g_param_spec_object("outputs-model", NULL, NULL, GTK_TYPE_TREE_MODEL, G_PARAM_READWRITE));

	output_changed_signal = g_signal_new("output-changed", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
on_stack_button_clicked(GtkWidget *button, Channel *channel)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
		return;
	if (channel->last_toggled == button) {
		channel->last_toggled = NULL;
	} else {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(channel->stack_hide), true);
	}
}

static void
on_stack_button_toggled(GtkWidget *button, Channel *channel)
{
	channel->last_toggled = button;
}

static void
connect_button_signals(GtkWidget *button, Channel *channel)
{
	if (button != channel->stack_hide) {
		g_signal_connect_after(button, "clicked", G_CALLBACK(on_stack_button_clicked), channel);
		g_signal_connect_after(button, "toggled", G_CALLBACK(on_stack_button_toggled), channel);
	}
}

static void
channel_init(Channel *self)
{
	gtk_widget_init_template(GTK_WIDGET(self));
	gtk_container_foreach(GTK_CONTAINER(self->stack_buttons), (GtkCallback)connect_button_signals, self);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->stack_hide), true);
}

int
channel_get_id(Channel *self)
{
	return self->id;
}

const char *
channel_get_name(Channel *self)
{
	return gtk_label_get_text(GTK_LABEL(self->ui.name));
}

void
channel_set_output_iter(Channel *self, GtkTreeIter *iter)
{
	if (self->ui.output)
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(self->ui.output), iter);
}

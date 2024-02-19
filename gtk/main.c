#include <stdbool.h>
#include <gtk/gtk.h>
#include "channel.h"
#include "mixer.h"
#include "scaleentry.h"
#include "../osc.h"

static const char *const input_names[] = {
	"Mic/Line 1",
	"Mic/Line 2",
	"Inst/Line 3",
	"Inst/Line 4",
	"Analog 5",
	"Analog 6",
	"Analog 7",
	"Analog 8",
	"SPDIF L",
	"SPDIF R",
	"AES L",
	"AES R",
	"ADAT 1",
	"ADAT 2",
	"ADAT 3",
	"ADAT 4",
	"ADAT 5",
	"ADAT 6",
	"ADAT 7",
	"ADAT 8",
};
static const char *const output_names[] = {
	"Analog 1",
	"Analog 2",
	"Analog 3",
	"Analog 4",
	"Analog 5",
	"Analog 6",
	"Phones 7",
	"Phones 8",
	"SPDIF L",
	"SPDIF R",
	"AES L",
	"AES R",
	"ADAT 1",
	"ADAT 2",
	"ADAT 3",
	"ADAT 4",
	"ADAT 5",
	"ADAT 6",
	"ADAT 7",
	"ADAT 8",
};

struct _OSCMixWindow {
	GtkApplicationWindow base;
	Mixer *osc;
	gpointer send_host;
	gpointer send_port;
	gpointer recv_host;
	gpointer recv_port;

	gpointer reverb_enabled;
	gpointer reverb_type;
	gpointer reverb_predelay;
	gpointer reverb_lowcut;
	gpointer reverb_roomscale;
	gpointer reverb_attack;
	gpointer reverb_hold;
	gpointer reverb_release;
	gpointer reverb_highcut;
	gpointer reverb_time;
	gpointer reverb_damp;
	gpointer reverb_smooth;
	gpointer reverb_volume;
	gpointer reverb_width;
	gpointer echo_enabled;
	gpointer echo_type;
	gpointer echo_delay;
	gpointer echo_volume;
	gpointer echo_feedback;
	gpointer echo_highcut;
	gpointer echo_width;
	gpointer durec_recordview;
	gpointer mainout;
	gpointer mainmono;
	gpointer muteenable;
	gpointer dimreduction;
	gpointer dim;
	gpointer recallvolume;
	gpointer clock_source;
	gpointer clock_samplerate;
	gpointer clock_wckout;
	gpointer clock_wcksingle;
	gpointer clock_wckterm;
	gpointer opticalout;
	gpointer spdifout;
	gpointer ccmix;
	gpointer standalonemidi;
	gpointer standalonearc;
	gpointer lockkeys;
	gpointer remapkeys;

	gpointer inputs;
	gpointer playbacks;
	gpointer outputs;
	GPtrArray *inputs_array;
	GtkListStore *outputs_store;
	GtkTreeModel *outputs_model;

	/* durec */
	GtkListStore *durec_files;
	GtkComboBox *durec_file;
	GtkCellView *durec_samplerate;
	GtkCellView *durec_channels;
	GtkAdjustment *durec_time;
};

G_DECLARE_FINAL_TYPE(OSCMixWindow, oscmix_window, OSCMIX, WINDOW, GtkApplicationWindow)
G_DEFINE_TYPE(OSCMixWindow, oscmix_window, GTK_TYPE_APPLICATION_WINDOW)

static void
on_reverb_type_changed(GtkComboBox *combo, gpointer ptr)
{
	OSCMixWindow *win;
	int type;

	win = OSCMIX_WINDOW(ptr);
	type = gtk_combo_box_get_active(combo);
	gtk_widget_set_sensitive(win->reverb_roomscale, type < 12);
	gtk_widget_set_sensitive(win->reverb_attack, type == 12);
	gtk_widget_set_sensitive(win->reverb_hold, type == 12 || type == 13);
	gtk_widget_set_sensitive(win->reverb_release, type == 12 || type == 13);
	gtk_widget_set_sensitive(win->reverb_time, type == 14);
	gtk_widget_set_sensitive(win->reverb_damp, type == 14);
}

static char *
format_durec_position(GtkScale *scale, double value, gpointer ptr)
{
	return g_strdup_printf("%.2d:%.2d:%.2d", (int)value / 3600, (int)value / 60 % 60, (int)value % 60);
}

static void
address_resolved(GObject *obj, GAsyncResult *res, gpointer ptr)
{
	OSCMixWindow *self;
	GSocketAddress *addr;
	GError *err;

	self = OSCMIX_WINDOW(ptr);
	err = NULL;
	addr = g_socket_address_enumerator_next_finish(G_SOCKET_ADDRESS_ENUMERATOR(obj), res, &err);
	if (!addr) {
		g_warning("failed to enumerate address: %s", err->message);
		return;
	}
	g_object_set(self->osc, "send-address", addr, NULL);
	g_object_unref(obj);

	mixer_send(self->osc, "/refresh", NULL);
}

static void
on_send_addr_changed(GtkEntry *entry, gpointer ptr)
{
	OSCMixWindow *self;
	GSocketConnectable *addr;
	GSocketAddressEnumerator *addrenum;

	self = OSCMIX_WINDOW(ptr);
	addr = g_network_address_new(gtk_entry_get_text(self->send_host), gtk_adjustment_get_value(self->send_port));
	addrenum = g_socket_connectable_enumerate(addr);
	g_socket_address_enumerator_next_async(addrenum, NULL, address_resolved, self);
	g_object_unref(addr);
}

static void
on_recv_addr_changed(GtkEntry *entry, gpointer ptr)
{
	OSCMixWindow *self;
	GSocketAddress *addr;

	self = OSCMIX_WINDOW(ptr);
	addr = g_inet_socket_address_new_from_string(gtk_entry_get_text(self->recv_host), gtk_adjustment_get_value(self->recv_port));
	g_object_set(self->osc, "recv-address", addr, (char *)0);
}

static void
on_output_visible_changed(GObject *obj, GParamSpec *pspec, gpointer ptr)
{
	OSCMixWindow *self;

	self = OSCMIX_WINDOW(ptr);
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(self->outputs_model));
}

static void
on_mainout_changed(GtkComboBox *combo, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeModel *model;
	GtkTreeIter iter;
	Channel *output;
	GObject *obj;

	self = OSCMIX_WINDOW(ptr);
	if (!gtk_combo_box_get_active_iter(combo, &iter))
		return;
	model = gtk_combo_box_get_model(combo);
	gtk_tree_model_get(model, &iter, 0, (gpointer)&output, -1);
	g_object_get(output, "right", &obj, (char *)0);
	if (obj) {
		GValue val = {0};

		g_value_init(&val, G_TYPE_INT);
		g_value_set_int(&val, (channel_get_id(output) - 1) / 2);
		mixer_send(self->osc, "/controlroom/mainout", &val);
	} else if (gtk_tree_model_iter_previous(model, &iter)) {
		gtk_combo_box_set_active_iter(combo, &iter);
	}
}

static void
on_mainout_osc(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	Channel *output;
	GValue val = {0};
	int id;

	if (len == 0)
		return;
	self = OSCMIX_WINDOW(ptr);
	g_value_init(&val, G_TYPE_INT);
	if (!g_value_transform(&arg[0], &val))
		return;
	if (gtk_tree_model_get_iter_first(self->outputs_model, &iter)) {
		id = g_value_get_int(&val);
		do {
			gtk_tree_model_get(self->outputs_model, &iter, 0, &output, -1);
			if ((channel_get_id(output) - 1) / 2 == id) {
				gtk_combo_box_set_active_iter(self->mainout, &iter);
				break;
			}
		} while (gtk_tree_model_iter_next(self->outputs_model, &iter));
	}
	
}

static void
on_samplerate_osc(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	char *text;

	if (len == 0)
		return;
	self = OSCMIX_WINDOW(ptr);
	if (G_VALUE_HOLDS_INT(&arg[0])) {
		text = g_strdup_printf("%d Hz", g_value_get_int(&arg[0]));
		gtk_label_set_label(self->clock_samplerate, text);
	}
}

static void
on_channel_output_changed(Channel *channel, GtkTreeIter *iter, gpointer ptr)
{
	static bool in_output_changed;
	OSCMixWindow *self;
	Channel *other;
	size_t i;

	if (in_output_changed)
		return;
	in_output_changed = true;
	self = OSCMIX_WINDOW(ptr);
	for (i = 0; i < self->inputs_array->len; ++i) {
		other = self->inputs_array->pdata[i];
		if (other != channel)
			channel_set_output_iter(other, iter);
	}
	in_output_changed = false;
}

static void
on_durec_name(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	int index;

	if (len < 2 || !G_VALUE_HOLDS_INT(&arg[0]) || !G_VALUE_HOLDS_STRING(&arg[1]))
		return;
	self = OSCMIX_WINDOW(ptr);
	index = g_value_get_int(&arg[0]);
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(self->durec_files), &iter, NULL, index))
		gtk_list_store_insert(self->durec_files, &iter, index);
	g_value_take_string(&arg[1], g_strdup_printf("%s.wav", g_value_get_string(&arg[1])));
	gtk_list_store_set_value(self->durec_files, &iter, 0, &arg[1]);
}

static void
on_durec_samplerate(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	int index;

	if (len < 2 || !G_VALUE_HOLDS_INT(&arg[0]) || !G_VALUE_HOLDS_INT(&arg[1]))
		return;
	self = OSCMIX_WINDOW(ptr);
	index = g_value_get_int(&arg[0]);
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(self->durec_files), &iter, NULL, index))
		gtk_list_store_insert(self->durec_files, &iter, index);
	gtk_list_store_set_value(self->durec_files, &iter, 1, &arg[1]);
}

static void
on_durec_channels(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	int index;

	if (len < 2 || !G_VALUE_HOLDS_INT(&arg[0]) || !G_VALUE_HOLDS_INT(&arg[1]))
		return;
	self = OSCMIX_WINDOW(ptr);
	index = g_value_get_int(&arg[0]);
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(self->durec_files), &iter, NULL, index))
		gtk_list_store_insert(self->durec_files, &iter, index);
	gtk_list_store_set_value(self->durec_files, &iter, 2, &arg[1]);
}

static void
on_durec_length(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	int index;

	if (len < 2 || !G_VALUE_HOLDS_INT(&arg[0]) || !G_VALUE_HOLDS_INT(&arg[1]))
		return;
	self = OSCMIX_WINDOW(ptr);
	index = g_value_get_int(&arg[0]);
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(self->durec_files), &iter, NULL, index))
		gtk_list_store_insert(self->durec_files, &iter, index);
	gtk_list_store_set_value(self->durec_files, &iter, 3, &arg[1]);
}

static void
on_durec_numfiles(GValue *arg, guint len, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	int numfiles;

	if (len == 0 || !G_VALUE_HOLDS_INT(&arg[0]))
		return;
	self = OSCMIX_WINDOW(ptr);
	numfiles = g_value_get_int(&arg[0]);
	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(self->durec_files), &iter, NULL, numfiles)) {
		while (gtk_list_store_remove(self->durec_files, &iter))
			;
	}
}

static void
on_durec_file_changed(GtkComboBox *combo, gpointer ptr)
{
	OSCMixWindow *self;
	GtkTreeIter iter;
	GtkTreePath *path;
	int length;

	self = OSCMIX_WINDOW(ptr);
	if (gtk_combo_box_get_active_iter(combo, &iter)) {
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->durec_files), &iter);
		gtk_cell_view_set_displayed_row(self->durec_samplerate, path);
		gtk_cell_view_set_displayed_row(self->durec_channels, path);
		gtk_tree_path_free(path);
		gtk_tree_model_get(GTK_TREE_MODEL(self->durec_files), &iter, 3, &length, -1);
		gtk_adjustment_set_upper(self->durec_time, length);
	}
}

static void
on_durec_stop(GtkButton *button, gpointer ptr)
{
	mixer_send(OSCMIX_WINDOW(ptr)->osc, "/durec/stop", NULL);
}

static void
on_durec_play(GtkButton *button, gpointer ptr)
{
	mixer_send(OSCMIX_WINDOW(ptr)->osc, "/durec/play", NULL);
}

static void
on_durec_record(GtkButton *button, gpointer ptr)
{
	mixer_send(OSCMIX_WINDOW(ptr)->osc, "/durec/record", NULL);
}

static void
on_durec_delete(GtkButton *button, gpointer ptr)
{
	OSCMixWindow *self;
	GValue val = {0};

	self = OSCMIX_WINDOW(ptr);
	g_value_init(&val, G_TYPE_INT);
	g_value_set_int(&val, gtk_combo_box_get_active(self->durec_file));
	mixer_send(self->osc, "/durec/delete", &val);
}

static void
oscmix_window_class_init(OSCMixWindowClass *class)
{
	g_type_ensure(scale_entry_get_type());
	gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(class), "/oscmix/oscmix.ui");
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, send_host);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, send_port);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, recv_host);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, recv_port);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_enabled);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_type);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_predelay);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_lowcut);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_roomscale);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_attack);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_hold);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_release);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_highcut);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_time);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_damp);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_smooth);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_volume);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, reverb_width);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_enabled);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_type);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_delay);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_feedback);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_highcut);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_volume);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, echo_width);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, durec_recordview);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, mainout);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, mainmono);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, muteenable);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, dimreduction);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, dim);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, recallvolume);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, clock_source);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, clock_samplerate);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, clock_wckout);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, clock_wcksingle);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, clock_wckterm);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, opticalout);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, spdifout);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, ccmix);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, standalonemidi);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, standalonearc);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, lockkeys);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, remapkeys);

	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, inputs);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, playbacks);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, outputs);

	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, durec_files);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, durec_file);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, durec_samplerate);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, durec_channels);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), OSCMixWindow, durec_time);

	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), format_durec_position);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_reverb_type_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_send_addr_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_recv_addr_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_mainout_changed);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_durec_stop);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_durec_play);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_durec_record);
	gtk_widget_class_bind_template_callback(GTK_WIDGET_CLASS(class), on_durec_delete);
}

static void
setup_channels(OSCMixWindow *self, ChannelType type, GtkBox *box)
{
	Channel *chan, *left;
	GtkWidget *sep;
	GtkTreeIter iter;
	int id;
	unsigned flags;

	left = NULL;
	for (id = 1; id <= 20; ++id) {
		flags = 0;
		if (type != CHANNEL_TYPE_PLAYBACK && id <= 8)
			flags |= CHANNEL_FLAG_ANALOG;
		if (id == 1 || id == 2)
			flags |= CHANNEL_FLAG_MIC;
		if (id == 3 || id == 4)
			flags |= CHANNEL_FLAG_INSTRUMENT;
		chan = g_object_new(channel_get_type(),
			"mixer", self->osc,
			"type", type,
			"flags", flags,
			"id", id,
			"name", type == CHANNEL_TYPE_INPUT ? input_names[id - 1] : output_names[id - 1],
			"outputs-model", type == CHANNEL_TYPE_OUTPUT ? NULL : self->outputs_model,
			(char *)0);
		if (left) {
			g_object_set(left, "right", chan, NULL);
			left = NULL;
		} else {
			left = chan;
		}
		gtk_widget_show(GTK_WIDGET(chan));
		gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(chan), false, false, 0);
		switch (type) {
		case CHANNEL_TYPE_INPUT:
		case CHANNEL_TYPE_PLAYBACK:
			g_ptr_array_add(self->inputs_array, chan);
			break;
		case CHANNEL_TYPE_OUTPUT:
			gtk_list_store_append(self->outputs_store, &iter);
			gtk_list_store_set(self->outputs_store, &iter, 0, chan, -1);
			g_signal_connect(chan, "notify::visible", G_CALLBACK(on_output_visible_changed), self);
			break;
		}
		g_object_bind_property(self->durec_recordview, "active", chan, "record-view", G_BINDING_DEFAULT);
		sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(sep), false, false, 0);
		g_object_bind_property(G_OBJECT(chan), "visible", G_OBJECT(sep), "visible", G_BINDING_DEFAULT);
		gtk_widget_show(sep);
		g_signal_connect(chan, "output-changed", G_CALLBACK(on_channel_output_changed), self);
	}
}

static gboolean
output_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer ptr)
{
	GValue child_val = G_VALUE_INIT;
	Channel *chan;

	gtk_tree_model_get_value(model, iter, 0, &child_val);
	chan = g_value_get_object(&child_val);
	return chan && gtk_widget_get_visible(GTK_WIDGET(chan));
}

static void
output_modify(GtkTreeModel *model, GtkTreeIter *iter, GValue *val, int col, gpointer ptr)
{
	GtkTreeModel *child_model;
	GtkTreeIter child_iter;
	GValue child_val = G_VALUE_INIT;
	Channel *chan;

	child_model = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, iter);
	gtk_tree_model_get_value(child_model, &child_iter, 0, &child_val);
	chan = g_value_get_object(&child_val);
	switch (col) {
	case 0: g_value_set_object(val, chan); break;
	case 1: g_value_set_string(val, channel_get_name(chan)); break;
	}
}

static void
oscmix_window_init(OSCMixWindow *self)
{
	GSettings *settings;

	gtk_widget_init_template(GTK_WIDGET(self));
	self->osc = mixer_new();

	settings = g_settings_new("oscmix");
	g_settings_bind(settings, "send-host", self->send_host, "text", G_SETTINGS_BIND_DEFAULT);
	g_settings_bind(settings, "send-port", self->send_port, "value", G_SETTINGS_BIND_DEFAULT);
	g_settings_bind(settings, "recv-host", self->recv_host, "text", G_SETTINGS_BIND_DEFAULT);
	g_settings_bind(settings, "recv-port", self->recv_port, "value", G_SETTINGS_BIND_DEFAULT);

	mixer_bind(self->osc, "/reverb", G_TYPE_INT, self->reverb_enabled, "active");
	mixer_bind(self->osc, "/reverb/type", G_TYPE_INT, self->reverb_type, "active");
	mixer_bind(self->osc, "/reverb/predelay", G_TYPE_INT, self->reverb_predelay, "value");
	mixer_bind(self->osc, "/reverb/lowcut", G_TYPE_INT, self->reverb_lowcut, "value");
	mixer_bind(self->osc, "/reverb/roomscale", G_TYPE_FLOAT, self->reverb_roomscale, "value");
	mixer_bind(self->osc, "/reverb/attack", G_TYPE_INT, self->reverb_attack, "value");
	mixer_bind(self->osc, "/reverb/hold", G_TYPE_INT, self->reverb_hold, "value");
	mixer_bind(self->osc, "/reverb/release", G_TYPE_INT, self->reverb_release, "value");
	mixer_bind(self->osc, "/reverb/highcut", G_TYPE_INT, self->reverb_highcut, "value");
	mixer_bind(self->osc, "/reverb/time", G_TYPE_FLOAT, self->reverb_time, "value");
	mixer_bind(self->osc, "/reverb/damp", G_TYPE_INT, self->reverb_damp, "value");
	mixer_bind(self->osc, "/reverb/smooth", G_TYPE_INT, self->reverb_smooth, "value");
	mixer_bind(self->osc, "/reverb/volume", G_TYPE_FLOAT, self->reverb_volume, "value");
	mixer_bind(self->osc, "/reverb/width", G_TYPE_INT, self->reverb_width, "value");
	mixer_bind(self->osc, "/echo", G_TYPE_INT, self->echo_enabled, "active");
	mixer_bind(self->osc, "/echo/type", G_TYPE_INT, self->echo_type, "active");
	mixer_bind(self->osc, "/echo/delay", G_TYPE_FLOAT, self->echo_delay, "value");
	mixer_bind(self->osc, "/echo/feedback", G_TYPE_INT, self->echo_feedback, "value");
	mixer_bind(self->osc, "/echo/highcut", G_TYPE_INT, self->echo_highcut, "active");
	mixer_bind(self->osc, "/echo/volume", G_TYPE_FLOAT, self->echo_volume, "value");
	mixer_bind(self->osc, "/echo/width", G_TYPE_INT, self->echo_width, "value");
	mixer_connect(self->osc, "/controlroom/mainout", on_mainout_osc, self);
	mixer_bind(self->osc, "/controlroom/mainmono", G_TYPE_INT, self->mainmono, "active");
	mixer_bind(self->osc, "/controlroom/muteenable", G_TYPE_INT, self->muteenable, "active");
	mixer_bind(self->osc, "/controlroom/dimreduction", G_TYPE_FLOAT, self->dimreduction, "value");
	mixer_bind(self->osc, "/controlroom/dim", G_TYPE_INT, self->dim, "active");
	mixer_bind(self->osc, "/controlroom/recallvolume", G_TYPE_FLOAT, self->recallvolume, "value");
	mixer_bind(self->osc, "/clock/source", G_TYPE_INT, self->clock_source, "active");
	mixer_connect(self->osc, "/clock/samplerate", on_samplerate_osc, self);
	mixer_bind(self->osc, "/clock/wckout", G_TYPE_INT, self->clock_wckout, "active");
	mixer_bind(self->osc, "/clock/wcksingle", G_TYPE_INT, self->clock_wcksingle, "active");
	mixer_bind(self->osc, "/clock/wckterm", G_TYPE_INT, self->clock_wckterm, "active");
	mixer_bind(self->osc, "/hardware/opticalout", G_TYPE_INT, self->opticalout, "active");
	mixer_bind(self->osc, "/hardware/spdifout", G_TYPE_INT, self->spdifout, "active");
	mixer_bind(self->osc, "/hardware/ccmix", G_TYPE_INT, self->ccmix, "active");
	mixer_bind(self->osc, "/hardware/standalonemidi", G_TYPE_INT, self->standalonemidi, "active");
	mixer_bind(self->osc, "/hardware/standalonearc", G_TYPE_INT, self->standalonearc, "active");
	mixer_bind(self->osc, "/hardware/lockkeys", G_TYPE_INT, self->lockkeys, "active");
	mixer_bind(self->osc, "/hardware/remapkeys", G_TYPE_INT, self->remapkeys, "active");
	mixer_connect(self->osc, "/durec/numfiles", on_durec_numfiles, self);
	mixer_connect(self->osc, "/durec/name", on_durec_name, self);
	mixer_connect(self->osc, "/durec/samplerate", on_durec_samplerate, self);
	mixer_connect(self->osc, "/durec/channels", on_durec_channels, self);
	mixer_connect(self->osc, "/durec/length", on_durec_length, self);
	mixer_bind(self->osc, "/durec/time", G_TYPE_INT, self->durec_time, "value");
	mixer_bind(self->osc, "/durec/file", G_TYPE_INT, self->durec_file, "active");
	g_signal_connect(self->durec_file, "changed", G_CALLBACK(on_durec_file_changed), self);

	self->inputs_array = g_ptr_array_new();
	self->outputs_store = gtk_list_store_new(1, channel_get_type());
	self->outputs_model = gtk_tree_model_filter_new(GTK_TREE_MODEL(self->outputs_store), NULL);
	gtk_tree_model_filter_set_modify_func(GTK_TREE_MODEL_FILTER(self->outputs_model), 2, (GType[]){channel_get_type(), G_TYPE_STRING}, output_modify, NULL, NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(self->outputs_model), output_visible, NULL, NULL);
	gtk_combo_box_set_model(self->mainout, self->outputs_model);
	setup_channels(self, CHANNEL_TYPE_OUTPUT, self->outputs);
	setup_channels(self, CHANNEL_TYPE_INPUT, self->inputs);
	setup_channels(self, CHANNEL_TYPE_PLAYBACK, self->playbacks);

	g_signal_emit_by_name(self->send_host, "activate");
	g_signal_emit_by_name(self->recv_host, "activate");
}

static void
activate(GtkApplication *app, gpointer unused)
{
	GtkCssProvider *css;
	GObject *win;

	css = gtk_css_provider_new();
	gtk_css_provider_load_from_resource(css, "/oscmix/oscmix.css");
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);

	win = g_object_new(oscmix_window_get_type(), "application", app, NULL);
	gtk_window_set_application(GTK_WINDOW(win), app);
	gtk_window_present(GTK_WINDOW(win));
}

int
main(int argc, char *argv[])
{
	GtkApplication *app;
	int ret;

	app = gtk_application_new(NULL,
#if GLIB_CHECK_VERSION(2, 74, 0)
		G_APPLICATION_DEFAULT_FLAGS
#else
		G_APPLICATION_FLAGS_NONE
#endif
	);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	ret = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	return ret;
}

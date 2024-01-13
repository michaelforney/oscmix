#include "scaleentry.h"

struct _ScaleEntry {
	GtkBox box;
	GtkEntry *entry;
	GtkScaleButton *button;
	GtkAdjustment *adjustment;
	int digits;
	char *unit;
};

enum {
	PROP_ADJUSTMENT = 1,
	PROP_DIGITS,
	PROP_UNIT,
	PROP_VALUE,
};

static GObject *
scale_entry_buildable_get_internal_child(GtkBuildable *buildable, GtkBuilder *builder, const char *name)
{
	if (strcmp(name, "entry") == 0)
		return G_OBJECT(OSCMIX_SCALE_ENTRY(buildable)->entry);
	else if (strcmp(name, "button") == 0)
		return G_OBJECT(OSCMIX_SCALE_ENTRY(buildable)->button);
	return NULL;
}

static void
scale_entry_buildable_init(GtkBuildableIface *iface)
{
	iface->get_internal_child = scale_entry_buildable_get_internal_child;
}

G_DEFINE_TYPE_WITH_CODE(ScaleEntry, scale_entry, GTK_TYPE_BOX,
	G_IMPLEMENT_INTERFACE(GTK_TYPE_BUILDABLE, scale_entry_buildable_init)
	G_IMPLEMENT_INTERFACE(GTK_TYPE_ORIENTABLE, NULL))

static void
on_value_changed(GtkAdjustment *adj, gpointer ptr)
{
	ScaleEntry *self;
	double val, max, min;
	char *text;

	self = OSCMIX_SCALE_ENTRY(ptr);
	val = gtk_adjustment_get_value(adj);
	max = gtk_adjustment_get_upper(adj);
	min = gtk_adjustment_get_lower(adj);
	//printf("val=%f min=%f max=%f %f\n", val, min, max, (val - min) / (max - min));
	gtk_entry_set_progress_fraction(self->entry, (val - min) / (max - min));
	text = g_strdup_printf("%.*f%s", self->digits, val, self->unit);
	if (strcmp(text, gtk_entry_get_text(self->entry)) != 0)
		gtk_entry_set_text(self->entry, text);
	g_free(text);
	g_object_notify(G_OBJECT(self), "value");
}

static void
on_activate(GtkEntry *entry, gpointer ptr)
{
	ScaleEntry *self;
	const char *text;
	double val;

	self = OSCMIX_SCALE_ENTRY(ptr);
	text = gtk_entry_get_text(entry);
	val = g_strtod(text, NULL);
	printf("activate %s %f\n", text, val);
	g_object_set(self, "value", val, NULL);
}

static void
set_width(ScaleEntry *self)
{
	double val;
	int width, ret;

	width = 0;
	if (self->adjustment) {
		val = gtk_adjustment_get_lower(self->adjustment);
		ret = snprintf(NULL, 0, "%.*f%s", self->digits, val, self->unit);
		width = MAX(width, ret);
		val = gtk_adjustment_get_upper(self->adjustment);
		ret = snprintf(NULL, 0, "%.*f%s", self->digits, val, self->unit);
		width = MAX(width, ret);
	}
	gtk_entry_set_width_chars(self->entry, width);
}

static void
scale_entry_set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec)
{
	ScaleEntry *self;

	self = OSCMIX_SCALE_ENTRY(obj);
	switch (id) {
	case PROP_ADJUSTMENT:
		if (self->adjustment) {
			g_signal_handlers_disconnect_by_data(self->adjustment, self);
			g_object_unref(self->adjustment);
		}
		self->adjustment = g_object_ref_sink(g_value_get_object(val));
		gtk_scale_button_set_adjustment(self->button, self->adjustment);
		if (self->adjustment) {
			g_signal_connect(self->adjustment, "value-changed", G_CALLBACK(on_value_changed), self);
			on_value_changed(self->adjustment, self);
			set_width(self);
		}
		break;
	case PROP_DIGITS:
		self->digits = g_value_get_int(val);
		set_width(self);
		break;
	case PROP_UNIT:
		g_free(self->unit);
		self->unit = g_value_dup_string(val);
		set_width(self);
		break;
	case PROP_VALUE:
		if (self->adjustment)
			gtk_adjustment_set_value(self->adjustment, g_value_get_double(val));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
		break;
	}
}

static void
scale_entry_get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec)
{
	ScaleEntry *self;

	self = OSCMIX_SCALE_ENTRY(obj);
	switch (id) {
	case PROP_ADJUSTMENT:
		g_value_set_object(val, G_OBJECT(self->adjustment));
		break;
	case PROP_DIGITS:
		g_value_set_int(val, self->digits);
		break;
	case PROP_UNIT:
		g_value_set_string(val, self->unit);
		break;
	case PROP_VALUE:
		if (self->adjustment)
			g_value_set_double(val, gtk_adjustment_get_value(self->adjustment));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
		break;
	}
}

static void
scale_entry_class_init(ScaleEntryClass *class)
{
	G_OBJECT_CLASS(class)->set_property = scale_entry_set_property;
	G_OBJECT_CLASS(class)->get_property = scale_entry_get_property;
	gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(class), "/oscmix/scaleentry.ui");
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ScaleEntry, entry);
	gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ScaleEntry, button);
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_ADJUSTMENT,
		g_param_spec_object("adjustment", NULL, NULL, GTK_TYPE_ADJUSTMENT, G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_DIGITS,
		g_param_spec_int("digits", NULL, NULL, 0, 10, 0, G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_UNIT,
		g_param_spec_string("unit", NULL, NULL, "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_VALUE,
		g_param_spec_double("value", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE));
}

static void
scale_entry_init(ScaleEntry *self)
{
	gtk_widget_init_template(GTK_WIDGET(self));
	g_signal_connect(self->entry, "activate", G_CALLBACK(on_activate), self);
	g_object_bind_property(self, "orientation", self->button, "orientation", G_BINDING_SYNC_CREATE);
}

GtkAdjustment *
scale_entry_get_adjustment(ScaleEntry *self)
{
	g_return_val_if_fail(OSCMIX_IS_SCALE_ENTRY(self), NULL);
	return self->adjustment;
}

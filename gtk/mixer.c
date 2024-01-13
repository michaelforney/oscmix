#include <stdbool.h>
#include <stdio.h>
#include <gtk/gtk.h> // XXX
#include <gio/gio.h>
#include "mixer.h"
#include "../osc.h"

struct _Mixer {
	GObject obj;
	GSocket *socket;
	GSocketAddress *send_address;
	GSocketAddress *recv_address;
	GHashTable *handlers;
	GSource *source;
} _Mixer;

typedef struct {
	Mixer *mixer;
	const char *addr;
	GType type;
	GValue val;
	GObject *obj;
	GParamSpec *pspec;
} Binding;

G_DEFINE_TYPE(Mixer, mixer, G_TYPE_OBJECT)

enum {
	PROP_SEND_ADDRESS = 1,
	PROP_RECV_ADDRESS,
};

static void
handle_packet(Mixer *self, struct oscmsg *msg)
{
	const char *addr;

	addr = oscgetstr(msg);
	if (msg->err) {
		g_warning("bad osc message: %s", msg->err);
		return;
	}
	if (strcmp(addr, "#bundle") == 0) {
		unsigned long len;
		struct oscmsg sub;

		oscgetint(msg);
		oscgetint(msg);
		while (msg->buf != msg->end) {
			len = oscgetint(msg);
			if (len > msg->end - msg->buf) {
				g_debug("%lu %td", len, msg->end - msg->buf);
				msg->err = "truncated bundle";
			}
			if (msg->err) {
				g_warning("bad osc packet: %s", msg->err);
				return;
			}
			sub.err = NULL;
			sub.buf = msg->buf;
			sub.end = msg->buf + len;
			sub.type = NULL;
			handle_packet(self, &sub);
			msg->buf = sub.end;
		}
	} else {
		GClosure *closure;

		if (*addr != '/') {
			g_warning("bad osc message: address does not start with '/'");
			return;
		}

		msg->type = oscgetstr(msg);
		if (msg->type && *msg->type != ',')
			msg->err = "type string does not start with ','";
		if (msg->err) {
			g_warning("bad osc message: %s", msg->err);
			return;
		}
		++msg->type;

		closure = g_hash_table_lookup(self->handlers, addr);
		if (closure) {
			static GArray *vals;
			GValue val = {0}, args[2] = {0};

			if (!vals)
				vals = g_array_new(false, false, sizeof val);
			g_array_set_size(vals, 0);
			while (*msg->type) {
				memset(&val, 0, sizeof val);
				switch (*msg->type) {
				case 's':
					g_value_init(&val, G_TYPE_STRING);
					g_value_set_static_string(&val, oscgetstr(msg));
					break;
				case 'i':
					g_value_init(&val, G_TYPE_INT);
					g_value_set_int(&val, oscgetint(msg));
					break;
				case 'f':
					g_value_init(&val, G_TYPE_FLOAT);
					g_value_set_float(&val, oscgetfloat(msg));
				default:
					break;
				}
				g_array_append_val(vals, val);
			}
			g_value_init(&args[0], G_TYPE_POINTER);
			g_value_set_pointer(&args[0], vals->data);
			g_value_init(&args[1], G_TYPE_UINT);
			g_value_set_uint(&args[1], vals->len);
			g_closure_invoke(closure, NULL, 2, args, NULL);
		}
	}
}

static gboolean
on_osc_data(GDatagramBased *socket, GIOCondition condition, gpointer ptr)
{
	Mixer *self;
	unsigned char buf[8192];
	struct oscmsg msg;
	gssize ret;

	self = OSCMIX_MIXER(ptr);
	ret = g_socket_receive(G_SOCKET(socket), (gchar *)buf, sizeof buf, NULL, NULL);
	msg.err = NULL;
	msg.buf = buf;
	msg.end = buf + ret;
	msg.type = NULL;
	handle_packet(self, &msg);
	return true;
}

static void
setup_connection(Mixer *self)
{
	if (self->source)
		g_source_unref(self->source);
	if (self->socket)
		g_object_unref(self->socket);
	self->socket = g_socket_new(g_socket_address_get_family(self->recv_address), G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);
	g_socket_bind(self->socket, self->recv_address, false, NULL);
	self->source = g_socket_create_source(self->socket, G_IO_IN, NULL);
	g_source_set_callback(self->source, (GSourceFunc)on_osc_data, self, NULL);
	g_source_attach(self->source, NULL);
}

static void
mixer_set_property(GObject *obj, guint id, const GValue *val, GParamSpec *spec)
{
	Mixer *self;

	self = OSCMIX_MIXER(obj);
	switch (id) {
	case PROP_SEND_ADDRESS:
		if (self->send_address)
			g_object_unref(self->send_address);
		self->send_address = G_SOCKET_ADDRESS(g_value_get_object(val));
		break;
	case PROP_RECV_ADDRESS:
		if (self->recv_address)
			g_object_unref(self->recv_address);
		self->recv_address = G_SOCKET_ADDRESS(g_value_get_object(val));
		setup_connection(self);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
		break;
	}
}

static void
mixer_get_property(GObject *obj, guint id, GValue *val, GParamSpec *spec)
{
	Mixer *self;

	self = OSCMIX_MIXER(obj);
	switch (id) {
	case PROP_SEND_ADDRESS:
		g_value_set_object(val, G_OBJECT(self->send_address));
		break;
	case PROP_RECV_ADDRESS:
		g_value_set_object(val, G_OBJECT(self->recv_address));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
		break;
	}
}

static void
mixer_class_init(MixerClass *class)
{
	G_OBJECT_CLASS(class)->set_property = mixer_set_property;
	G_OBJECT_CLASS(class)->get_property = mixer_get_property;
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_SEND_ADDRESS,
		g_param_spec_object("send-address", NULL, NULL, G_TYPE_SOCKET_ADDRESS, G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(class), PROP_RECV_ADDRESS,
		g_param_spec_object("recv-address", NULL, NULL, G_TYPE_SOCKET_ADDRESS, G_PARAM_READWRITE));
}

static void
mixer_init(Mixer *self)
{
	self->handlers = g_hash_table_new(g_str_hash, g_str_equal);
}

Mixer *
mixer_new(void)
{
	return g_object_new(mixer_get_type(), NULL);
}

void
mixer_send(Mixer *self, const char *addr, GValue *val)
{
	struct oscmsg msg;
	unsigned char buf[512];
	gssize ret;
	GError *err;

	if (!self->socket)
		return;
	msg.type = NULL;
	msg.buf = buf;
	msg.end = buf + sizeof buf;
	msg.err = NULL;
	oscputstr(&msg, addr);
	if (val) {
		switch (G_VALUE_TYPE(val)) {
		case G_TYPE_STRING:
			oscputstr(&msg, ",s");
			oscputstr(&msg, g_value_get_string(val));
			break;
		case G_TYPE_BOOLEAN:
			oscputstr(&msg, ",i");
			oscputint(&msg, g_value_get_boolean(val));
			break;
		case G_TYPE_INT:
			oscputstr(&msg, ",i");
			oscputint(&msg, g_value_get_int(val));
			break;
		case G_TYPE_FLOAT:
			oscputstr(&msg, ",f");
			oscputfloat(&msg, g_value_get_float(val));
			break;
		default:
			g_critical("unknown value type '%s'", g_type_name(G_VALUE_TYPE(val)));
			return;
		}
	} else {
		oscputstr(&msg, ",");
	}
	err = NULL;
	ret = g_socket_send_to(self->socket, self->send_address, (char *)buf, msg.buf - buf, NULL, &err);
	if (ret < 0)
		g_warning("%s", err->message);
}

static bool in_osc_to_prop;

static void
prop_to_osc(GObject *obj, GParamSpec *pspec, gpointer data)
{
	Mixer *self;
	GValue val;
	Binding *bind;

	if (in_osc_to_prop)
		return;
	bind = data;
	self = bind->mixer;
	memset(&val, 0, sizeof val);
	g_object_get_property(obj, pspec->name, &val);
	if (g_param_values_cmp(pspec, &bind->val, &val) == 0)
		return;
	/*
	fprintf(stderr, "%s %s %s\n", bind->addr, g_type_name(G_VALUE_TYPE(&bind->val)), g_type_name(G_VALUE_TYPE(&val)));
	switch (G_VALUE_TYPE(&bind->val)) {
	case G_TYPE_INT: fprintf(stderr, "\told=%d\n", g_value_get_int(&bind->val)); break;
	case G_TYPE_DOUBLE: fprintf(stderr, "\told=%f\n", g_value_get_double(&bind->val)); break;
	}
	switch (G_VALUE_TYPE(&val)) {
	case G_TYPE_INT: fprintf(stderr, "\tnew=%d\n", g_value_get_int(&val)); break;
	case G_TYPE_DOUBLE: fprintf(stderr, "\tnew=%f\n", g_value_get_double(&val)); break;
	}
	*/
	g_value_copy(&val, &bind->val);
	memset(&val, 0, sizeof val);
	g_value_init(&val, bind->type);
	if (!g_value_transform(&bind->val, &val)) {
		g_warning("could not transform");
		return;
	}
	mixer_send(self, bind->addr, &val);
}

static void
osc_to_prop(GValue *arg, guint len, gpointer data)
{
	Binding *bind;

	bind = data;
	if (len == 0)
		return;
	if (g_value_transform(&arg[0], &bind->val)) {
		g_param_value_validate(bind->pspec, &bind->val);
		in_osc_to_prop = true;
		g_object_set_property(bind->obj, bind->pspec->name, &bind->val);
		in_osc_to_prop = false;
	}
}

void
mixer_bind(Mixer *self, char *addr, GType type, gpointer ptr, const char *prop)
{
	static guint notify_id;
	Binding *bind;
	GQuark detail;
	GClosure *closure;

	if (notify_id == 0) {
		notify_id = g_signal_lookup("notify", G_TYPE_OBJECT);
		g_assert(notify_id != 0);
	}
	bind = g_malloc(sizeof *bind);
	bind->mixer = self;
	bind->addr = addr;
	memset(&bind->val, 0, sizeof bind->val);
	bind->obj = G_OBJECT(ptr);
	bind->pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(bind->obj), prop);
	if (!bind->pspec) {
		g_critical("object '%s' has no property '%s'", gtk_widget_get_name(GTK_WIDGET(bind->obj)), prop);
		return;
	}
	bind->type = type;
	g_value_init(&bind->val, G_PARAM_SPEC_VALUE_TYPE(bind->pspec));
	detail = g_quark_from_string(prop);
	if (type != G_TYPE_NONE) {
		closure = g_cclosure_new(G_CALLBACK(prop_to_osc), bind, NULL);
		g_signal_connect_closure_by_id(bind->obj, notify_id, detail, closure, false);
	}
	mixer_connect(self, addr, osc_to_prop, bind);
}

void
mixer_connect(Mixer *self, char *addr, MixerCallback func, gpointer ptr)
{
	GClosure *closure;

	closure = g_cclosure_new(G_CALLBACK(func), ptr, NULL);
	g_closure_set_marshal(closure, g_cclosure_marshal_VOID__UINT);
	g_hash_table_insert(self->handlers, addr, closure);
}

static gboolean
should_remove(gpointer key, gpointer val, gpointer ptr)
{
	GClosure *closure;

	closure = val;
	return closure->data == ptr;
}

void
mixer_disconnect_by_data(Mixer *self, gpointer ptr)
{
	g_hash_table_foreach_remove(self->handlers, should_remove, ptr);
}

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dbus/dbus.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "SDL_events.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include "SDL_sensorfw.h"

#define SERVICE_NAME "com.nokia.SensorService"
#define MANAGER_PATH "/SensorManager"
#define MANAGER_IFACE "local.SensorManager"

#define SINT16_MAX 32767
#define SINT16_MIN (-32768)

#if 0
#define DPRINT(...) printf(__VA_ARGS__)
#else
#define DPRINT(...)
#endif

#if SDL_JOYSTICK_LINUX_SENSORFW

int sdl_sfw_num_joysticks = 0;

static DBusConnection *system_bus;

typedef enum {
	ACCELEROMETER = 0, ROTATION, COMPASS, PROXIMITY, SENSOR_COUNT
} SensorType;

typedef struct {
	long long timestamp;
	int x, y, z;
} TimedXyzData;

typedef struct {
	SensorType type;
	const char *sfw_name;
	const char *dbus_path;
	const char *iface_name;
	const char *name;
	unsigned int pkt_sz;
	int interval;
	int sdl_id;
	int session_id;
	int fd;
} Sensor;

typedef TimedXyzData AccelerometerData;
static AccelerometerData last_accelerometer;

typedef TimedXyzData RotationData;
static AccelerometerData last_rotation;

typedef struct {
	long long timestamp;
	int degrees;
	int raw_degrees;
	int corrected_degrees;
	int level;
} CompassData;
static CompassData last_compass;

typedef struct {
	long long timestamp;
	unsigned int value;
	unsigned char close;
} ProximityData;
static ProximityData last_proximity;

#define SENSOR_INIT(type, sfw, iface, name, strct, interval) \
	{ type, sfw, MANAGER_PATH "/" sfw, iface, name, sizeof(strct), \
	  interval, -1, -1, -1 }

static Sensor sensors[] = {
	SENSOR_INIT(ACCELEROMETER, "accelerometersensor", "local.AccelerometerSensor", "SensorFW Accelerometer", AccelerometerData, 50),
	SENSOR_INIT(ROTATION, "rotationsensor", "local.RotationSensor", "SensorFW Rotation", RotationData, 50),
	SENSOR_INIT(COMPASS, "compasssensor", "local.CompassSensor", "SensorFW Compass", CompassData, 100),
	SENSOR_INIT(PROXIMITY, "proximitysensor", "local.ProximitySensor", "SensorFW Proximity", ProximityData, 0),
};

static inline Sint16 scale(long v, long offset, long divider)
{
	long s = ((v + offset) * SINT16_MAX) / divider;
	if (s > SINT16_MAX) return SINT16_MAX;
	if (s < SINT16_MIN) return SINT16_MIN;
	return s;
}

static void update_axis(SDL_Joystick *joystick, Uint8 axis,
  int value, int *last, int offset, int divider)
{
	if (value != *last) {
		Sint16 scaled = scale(value, offset, divider);
		SDL_PrivateJoystickAxis(joystick, axis, scaled);
		*last = value;
	}
}

static void update_button(SDL_Joystick *joystick, Uint8 button,
  char value, char *last)
{
	/* Compare the boolean value of both variables. */
	if (!value != !(*last)) {
		SDL_PrivateJoystickButton(joystick, button,
			value ? SDL_PRESSED : SDL_RELEASED);
		*last = value;
	}
}

static void process_packet(SDL_Joystick *joystick, Sensor *s, void *data)
{
	switch (s->type) {
		case ACCELEROMETER: {
			AccelerometerData *d = (AccelerometerData *)data;
			update_axis(joystick, 0, d->x, &last_accelerometer.x, 0, 2000);
			update_axis(joystick, 1, -d->y, &last_accelerometer.y, 0, 2000);
			update_axis(joystick, 2, d->z, &last_accelerometer.z, 0, 2000);
		}
		break;
		case ROTATION: {
			RotationData *d = (RotationData *)data;
			update_axis(joystick, 0, d->x, &last_rotation.x, 0, 180);
			update_axis(joystick, 1, d->y, &last_rotation.y, 0, 180);
			update_axis(joystick, 2, d->z, &last_rotation.z, 0, 180);
		}
		break;
		case COMPASS: {
			CompassData *d = (CompassData *)data;
			update_axis(joystick, 0, d->degrees, &last_compass.degrees, 0, 359);
			update_axis(joystick, 1, d->raw_degrees, &last_compass.raw_degrees, 0, 359);
			update_axis(joystick, 2, d->level, &last_compass.level, 0, 3);
		}
		break;
		case PROXIMITY: {
			ProximityData *d = (ProximityData *)data;
			update_axis(joystick, 0, d->value, &last_proximity.value, 0, 1023);
			update_button(joystick, 0, d->close, &last_proximity.close);
		}
		break;
	}
}

static Sensor * find_sensor_by_sdl_id(int sdl_id)
{
	int i;
	for (i = 0; i < SENSOR_COUNT; i++) {
		if (sensors[i].sdl_id == sdl_id) {
			return &sensors[i];
		}
	}
	return NULL;
}

static int load_plugin_for_sensor(Sensor *s)
{
	DBusMessage *msg, *reply;
	dbus_bool_t res;

	msg = dbus_message_new_method_call(SERVICE_NAME, MANAGER_PATH,
		MANAGER_IFACE, "loadPlugin");
	if (!msg) return -1;

	dbus_message_append_args(msg,
		DBUS_TYPE_STRING, &s->sfw_name,
		DBUS_TYPE_INVALID);

	reply = dbus_connection_send_with_reply_and_block(system_bus, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)	return -1;

	dbus_message_get_args(reply, NULL,
		DBUS_TYPE_BOOLEAN, &res,
		DBUS_TYPE_INVALID);
	dbus_message_unref(reply);

	return res ? 0 : -1;
}

static int request_sensor(Sensor *s)
{
	DBusMessage *msg, *reply;
	dbus_int32_t session_id;
	dbus_int64_t pid = getpid();

	DPRINT("Trying to request sensor %s\n", s->sfw_name);

	msg = dbus_message_new_method_call(SERVICE_NAME, MANAGER_PATH,
		MANAGER_IFACE, "requestSensor");
	if (!msg) return -1;

	dbus_message_append_args(msg,
		DBUS_TYPE_STRING, &s->sfw_name,
		DBUS_TYPE_INT64, &pid,
		DBUS_TYPE_INVALID);

	reply = dbus_connection_send_with_reply_and_block(system_bus, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)	return -1;

	dbus_message_get_args(reply, NULL,
		DBUS_TYPE_INT32, &session_id,
		DBUS_TYPE_INVALID);
	dbus_message_unref(reply);

	DPRINT("Got sensor %s session %d\n", s->sfw_name, session_id);

	if (session_id >= 0) {
		s->session_id = session_id;
		return 0;
	} else {
		return -1;
	}
}

static int release_sensor(Sensor *s)
{
	DBusMessage *msg, *reply;
	dbus_int64_t pid = getpid();
	dbus_bool_t res;

	msg = dbus_message_new_method_call(SERVICE_NAME, MANAGER_PATH,
		MANAGER_IFACE, "releaseSensor");
	if (!msg) return -1;

	dbus_message_append_args(msg,
		DBUS_TYPE_STRING, &s->sfw_name,
		DBUS_TYPE_INT32, &s->session_id,
		DBUS_TYPE_INT64, &pid,
		DBUS_TYPE_INVALID);

	reply = dbus_connection_send_with_reply_and_block(system_bus, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)	return -1;

	dbus_message_get_args(reply, NULL,
		DBUS_TYPE_BOOLEAN, &res,
		DBUS_TYPE_INVALID);
	dbus_message_unref(reply);

	if (res) {
		DPRINT("Sensor %s session %d released\n", s->sfw_name, s->session_id);
		s->session_id = -1;
		return 0;
	} else {
		return -1;
	}
}

static int start_sensor(Sensor *s)
{
	DBusMessage *msg, *reply;

	msg = dbus_message_new_method_call(SERVICE_NAME, s->dbus_path, s->iface_name,
		"start");
	if (!msg) return -1;

	dbus_message_append_args(msg,
		DBUS_TYPE_INT32, &s->session_id,
		DBUS_TYPE_INVALID);

	reply = dbus_connection_send_with_reply_and_block(system_bus, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)	return -1;

	dbus_message_unref(reply);

	DPRINT("Sensor %s is now started\n", s->sfw_name);

	return 0;
}

static int stop_sensor(Sensor *s)
{
	DBusMessage *msg, *reply;

	msg = dbus_message_new_method_call(SERVICE_NAME, s->dbus_path, s->iface_name,
		"stop");
	if (!msg) return -1;

	dbus_message_append_args(msg,
		DBUS_TYPE_INT32, &s->session_id,
		DBUS_TYPE_INVALID);

	reply = dbus_connection_send_with_reply_and_block(system_bus, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)	return -1;

	dbus_message_unref(reply);

	DPRINT("Sensor %s is now stopped\n", s->sfw_name);

	return 0;
}

static int configure_sensor_interval(Sensor *s)
{
	DBusMessage *msg, *reply;

	msg = dbus_message_new_method_call(SERVICE_NAME, s->dbus_path, s->iface_name,
		"setInterval");
	if (!msg) return -1;

	dbus_message_append_args(msg,
		DBUS_TYPE_INT32, &s->session_id,
		DBUS_TYPE_INT32, &s->interval,
		DBUS_TYPE_INVALID);

	reply = dbus_connection_send_with_reply_and_block(system_bus, msg, -1, NULL);
	dbus_message_unref(msg);
	if (!reply)	return -1;

	dbus_message_unref(reply);

	DPRINT("Sensor %s is configured with interval %dms\n",
		s->sfw_name, s->interval);

	return 0;
}

static int open_socket(Sensor *s)
{
	struct sockaddr_un addr = { 0 };
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	int rc;
	char tag;

	if (fd < 0) return -1;

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "/tmp/sensord.sock");

	rc = connect(fd, (struct sockaddr*)&addr, SUN_LEN(&addr));
	if (rc < 0) {
		close(fd);
		return rc;
	}

	rc = send(fd, &s->session_id, sizeof(s->session_id), 0);
	if (rc < 0) {
		close(fd);
		return rc;
	}

	rc = recv(fd, &tag, 1, 0);
	if (rc < 0) {
		close(fd);
		return rc;
	}

	/* Close on exec() */
	rc = fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (rc < 0) {
		close(fd);
		return rc;
	}

	DPRINT("Opened socket fd=%d for %s session %d\n",
		fd, s->sfw_name, s->session_id);
	s->fd = fd;

	return 0;
}

static void close_socket(Sensor *s)
{
	DPRINT("Closing socket %d for %s\n", s->fd, s->sfw_name);
	close(s->fd);
	s->fd = -1;
}

static void flush_socket(Sensor *s)
{
	static char buf[128];
	int avail, rc;

	do {
		rc = ioctl(s->fd, FIONREAD, &avail);
		if (rc < 0) return;

		rc = read(s->fd, buf, sizeof(buf));
		if (rc < 0) return;
	} while (avail > 0);
}

static ssize_t read_socket_block(Sensor *s, void *buf, size_t size)
{
	char *b = buf;
	size_t bytes = 0, rem;

	while ((rem = size - bytes) > 0) {
		ssize_t r = read(s->fd, b, rem);
		if (r <= 0) return r;

		bytes += r;
		b += r;
	}

	return bytes;
}

void SDL_SFW_Init()
{
	int i;

	system_bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, NULL);
	if (!system_bus) return;
	dbus_connection_set_exit_on_disconnect(system_bus, FALSE);

	sdl_sfw_num_joysticks = 0;

	for (i = 0; i < SENSOR_COUNT; i++) {
		if (load_plugin_for_sensor(&sensors[i]) == 0) {
			sensors[i].sdl_id = sdl_sfw_num_joysticks;
			sdl_sfw_num_joysticks++;
			DPRINT("Found %s in sensor framework\n", sensors[i].sfw_name);
		}
	}
}

void SDL_SFW_Quit()
{
	int i;
	for (i = 0; i < SENSOR_COUNT; i++) {
		if (sensors[i].session_id != -1) {
			stop_sensor(&sensors[i]);
			release_sensor(&sensors[i]);
		}
		if (sensors[i].fd != -1) {
			close_socket(&sensors[i]);
		}
		sensors[i].sdl_id = -1;
	}

	dbus_connection_close(system_bus);
	dbus_connection_unref(system_bus);
	system_bus = NULL;

	sdl_sfw_num_joysticks = 0;
}

extern const char *SDL_SFW_JoystickName(int index)
{
	Sensor *s = find_sensor_by_sdl_id(index);
	if (s) {
		return s->name;
	}
	return NULL;
}

extern int SDL_SFW_JoystickOpen(SDL_Joystick *joystick)
{
	Sensor *s = find_sensor_by_sdl_id(joystick->index);
	int rc;

	rc = request_sensor(s);
	if (rc < 0) {
		SDL_SetError("Failed to request the sensor to sensord");
		return -1;
	}
	rc = open_socket(s);
	if (rc < 0) {
		SDL_SetError("Failed to open a socket with sensord");
		return -1;
	}

	rc = start_sensor(s);
	if (rc < 0) {
		SDL_SetError("Failed to start sensor");
		return -1;
	}

	rc = configure_sensor_interval(s);
	if (rc < 0) {
		fprintf(stderr, "Failed to configure sensor '%s' interval to '%d' ms.\n",
			s->sfw_name, s->interval);
		/* This is not fatal. */
	}

	switch (s->type) {
		case ACCELEROMETER:
			joystick->naxes = 3;
			break;
		case ROTATION:
			joystick->naxes = 3;
			break;
		case COMPASS:
			joystick->naxes = 3;
			break;
		case PROXIMITY:
			joystick->naxes = 1;
			joystick->nbuttons = 1;
			break;
	}

	joystick->hwdata = (void*) s;
	return 0;
}

extern void SDL_SFW_JoystickUpdate(SDL_Joystick *joystick)
{
	static char buffer[128];
	Sensor *s = (Sensor*)joystick->hwdata;
	int avail, rc;
	unsigned int count;
	const unsigned int max = (sizeof(buffer) - sizeof(count)) / s->pkt_sz;

	if (s->fd == -1) return;

	rc = ioctl(s->fd, FIONREAD, &avail);
	if (rc < 0) return;

	while (avail >= sizeof(count)) {
		rc = read_socket_block(s, &count, sizeof(count));
		if (rc < 0) return;
		if (count == 0) return;
		if (count > max) {
			/* Too many packets! */
			DPRINT("Socket overrun\n");
			flush_socket(s);
			return;
		}

		while (count > 0) {
			rc = read_socket_block(s, buffer, s->pkt_sz);
			if (rc < 0) return;

			/* Process a received packet! */
			process_packet(joystick, s, buffer);

			count--;
		}

		rc = ioctl(s->fd, FIONREAD, &avail);
	}
}

extern void SDL_SFW_JoystickClose(SDL_Joystick *joystick)
{
	Sensor *s = (Sensor*)joystick->hwdata;

	stop_sensor(s);
	release_sensor(s);
	close_socket(s);
}

#endif

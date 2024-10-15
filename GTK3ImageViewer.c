#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define SLIDESHOW_INTERVAL 3000 // 3 seconds

typedef struct {
    GdkMonitor *monitor;
    GtkWindow *window;
    GtkWidget *scrolled_window;
    gboolean shrink_to_fit;
    gboolean slideshow_active;
    gboolean is_fullscreen;
    gboolean actual_size;
    gboolean options_visible;
    guint timeout_id;
    int width;
    int height;
    GtkWidget *options_window;
} MonitorData;

static GList *images = NULL;
static GList *current_image = NULL;
static MonitorData *monitor_data = NULL;
static int num_monitors = 0;
static char **global_argv = NULL;


static gboolean has_image_extension(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext != NULL) {
        ext++; // Skip the dot
        char *lower_ext = g_ascii_strdown(ext, -1);
        gboolean is_image = g_strcmp0(lower_ext, "jpg") == 0 || g_strcmp0(lower_ext, "jpeg") == 0 || g_strcmp0(lower_ext, "png") == 0;
        g_free(lower_ext);
        return is_image;
    }
    return FALSE;
}

static GList* get_image_files(const char *directory) {
    GList *images = NULL;
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir) {
        const char *filename;
        while ((filename = g_dir_read_name(dir)) != NULL) {
            if (has_image_extension(filename)) {
                char *filepath = g_build_filename(directory, filename, NULL);
                images = g_list_append(images, filepath);
            }
        }
        g_dir_close(dir);
    }
    return images;
}

static GdkPixbuf* rotate_pixbuf(GdkPixbuf *pixbuf, int orientation) {
    switch (orientation) {
        case 3:
            return gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
        case 6:
            return gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
        case 8:
            return gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
        default:
            return g_object_ref(pixbuf);
    }
}

static int get_exif_orientation(const char *image_path) {
#ifdef DEBUG
    g_debug("Showing image: %s", image_path);
#endif
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(image_path, NULL);
    if (!pixbuf) {
        g_warning("Failed to load image: %s", image_path);
        return 1;
    }

    const char *orientation_str = gdk_pixbuf_get_option(pixbuf, "orientation");
    int orientation = orientation_str ? atoi(orientation_str) : 1;

    g_object_unref(pixbuf);
    return orientation;
}

static void show_image(MonitorData *data, const char *image_path) {
    g_debug("Showing image: %s", image_path);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(image_path, NULL);
    if (!pixbuf) {
        g_warning("Failed to load image: %s", image_path);
        return;
    }

    int orientation = get_exif_orientation(image_path);
    GdkPixbuf *rotated_pixbuf = rotate_pixbuf(pixbuf, orientation);
    g_object_unref(pixbuf);

    int width = gdk_pixbuf_get_width(rotated_pixbuf);
    int height = gdk_pixbuf_get_height(rotated_pixbuf);

    if (data->shrink_to_fit && (width > data->width || height > data->height)) {
        double aspect_ratio = (double)width / height;
        int new_width = data->width;
        int new_height = data->height;

        if (width > height) {
            new_height = (int)(data->width / aspect_ratio);
            if (new_height > data->height) {
                new_height = data->height;
                new_width = (int)(data->height * aspect_ratio);
            }
        } else {
            new_width = (int)(data->height * aspect_ratio);
            if (new_width > data->width) {
                new_width = data->width;
                new_height = (int)(data->width / aspect_ratio);
            }
        }

        GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(rotated_pixbuf, new_width, new_height, GDK_INTERP_BILINEAR);
        g_object_unref(rotated_pixbuf);
        rotated_pixbuf = scaled_pixbuf;
    }

    GtkWidget *image = gtk_image_new_from_pixbuf(rotated_pixbuf);
    g_object_unref(rotated_pixbuf);

    gtk_widget_set_hexpand(image, TRUE);
    gtk_widget_set_vexpand(image, TRUE);

    GtkWidget *child = gtk_bin_get_child(GTK_BIN(data->scrolled_window));
    if (child != NULL) {
        gtk_container_remove(GTK_CONTAINER(data->scrolled_window), child);
    }

    gtk_container_add(GTK_CONTAINER(data->scrolled_window), image);
    gtk_widget_show_all(GTK_WIDGET(data->scrolled_window));
}

static void show_image_by_direction(gboolean next) {
    if (current_image == NULL) {
        current_image = images;
    } else {
        current_image = next ? g_list_next(current_image) : g_list_previous(current_image);
        if (current_image == NULL) {
            current_image = next ? images : g_list_last(images);
        }
    }

    char *image_path = (char *)current_image->data;
#ifdef DEBUG
    g_debug("Navigating to image: %s", image_path);
#endif
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(image_path, NULL);
    if (!pixbuf) {
        g_warning("Failed to load image: %s", image_path);
        return;
    }

    int orientation = get_exif_orientation(image_path);
    GdkPixbuf *rotated_pixbuf = rotate_pixbuf(pixbuf, orientation);
    g_object_unref(pixbuf);

    int width = gdk_pixbuf_get_width(rotated_pixbuf);
    int height = gdk_pixbuf_get_height(rotated_pixbuf);
    g_object_unref(rotated_pixbuf);

    MonitorData *best_monitor = NULL;
    int best_scale_down = INT_MAX;

    for (int i = 0; i < num_monitors; i++) {
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(monitor_data[i].window), &allocation);
        int window_width = allocation.width;
        int window_height = allocation.height;

        int scale_down_width = (width > window_width) ? width - window_width : 0;
        int scale_down_height = (height > window_height) ? height - window_height : 0;
        int scale_down = (scale_down_width > scale_down_height) ? scale_down_width : scale_down_height;

        if (scale_down < best_scale_down) {
            best_monitor = &monitor_data[i];
            best_scale_down = scale_down;
        }
    }

    if (best_monitor != NULL) {
        show_image(best_monitor, image_path);
    }
}

static gboolean on_timeout(gpointer user_data) {
    show_image_by_direction(TRUE);
    return G_SOURCE_CONTINUE;
}

static void restart_slideshow() {
    for (int i = 0; i < num_monitors; i++) {
        if (monitor_data[i].slideshow_active) {
            if (monitor_data[i].timeout_id != 0) {
                g_source_remove(monitor_data[i].timeout_id);
            }
            monitor_data[i].timeout_id = g_timeout_add(SLIDESHOW_INTERVAL, on_timeout, NULL);
        }
    }
}

static void toggle_options_window(MonitorData *data) {
    if (data->options_visible) {
        gtk_widget_hide(data->options_window);
        data->options_visible = FALSE;
    } else {
        gtk_widget_show_all(data->options_window);
        data->options_visible = TRUE;
    }
}

static void create_options_window(MonitorData *data) {
    data->options_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(data->options_window), "Options");
    gtk_window_set_default_size(GTK_WINDOW(data->options_window), 200, 200);
    gtk_window_set_keep_above(GTK_WINDOW(data->options_window), TRUE);

    GtkWidget *label = gtk_label_new(
        "Key Press Options:\n"
        "F/Escape: Toggle Fullscreen\n"
        "Right Arrow: Next Image\n"
        "Left Arrow: Previous Image\n"
        "Space: Toggle Shrink to Fit\n"
        "S: Toggle Slideshow\n"
        "A: Toggle Actual Size\n"
        "O: Toggle Options"
    );
    gtk_container_add(GTK_CONTAINER(data->options_window), label);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    MonitorData *data = (MonitorData *)user_data;
    if (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_Escape) {
        if (data->is_fullscreen) {
            gtk_window_unfullscreen(GTK_WINDOW(data->window));
            data->is_fullscreen = FALSE;
        } else {
            gtk_window_fullscreen(GTK_WINDOW(data->window));
            data->is_fullscreen = TRUE;
        }
    } else if (event->keyval == GDK_KEY_space && !(event->state & GDK_CONTROL_MASK)) {
        show_image_by_direction(TRUE);
        restart_slideshow();
    } else if (event->keyval == GDK_KEY_space && (event->state & GDK_CONTROL_MASK)) {
        show_image_by_direction(FALSE);
        restart_slideshow();
    } else if (event->keyval == GDK_KEY_r) {
        for (int i = 0; i < num_monitors; i++) {
            monitor_data[i].shrink_to_fit = !monitor_data[i].shrink_to_fit;
        }
        show_image((MonitorData *)user_data, (char *)current_image->data);
    } else if (event->keyval == GDK_KEY_s) {
        for (int i = 0; i < num_monitors; i++) {
            monitor_data[i].slideshow_active = !monitor_data[i].slideshow_active;
            if (monitor_data[i].slideshow_active) {
                restart_slideshow();
            } else {
                if (monitor_data[i].timeout_id != 0) {
                    g_source_remove(monitor_data[i].timeout_id);
                    monitor_data[i].timeout_id = 0;
                }
            }
        }
    } else if (event->keyval == GDK_KEY_a) {
        for (int i = 0; i < num_monitors; i++) {
            monitor_data[i].actual_size = !monitor_data[i].actual_size;
        }
        show_image((MonitorData *)user_data, (char *)current_image->data);
    } else if (event->keyval == GDK_KEY_o) {
        for (int i = 0; i < num_monitors; i++) {
            toggle_options_window(&monitor_data[i]);
        }
    } else if (data->actual_size) {
        int dx = 0, dy = 0;
        if (event->keyval == GDK_KEY_Up) {
            dy = -10;
        } else if (event->keyval == GDK_KEY_Down) {
            dy = 10;
        } else if (event->keyval == GDK_KEY_Left) {
            dx = -10;
        } else if (event->keyval == GDK_KEY_Right) {
            dx = 10;
        }
        if (dx != 0 || dy != 0) {
            GtkWidget *child = gtk_bin_get_child(GTK_BIN(data->scrolled_window));
            if (child != NULL) {
                GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(data->scrolled_window));
                GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(data->scrolled_window));
                gtk_adjustment_set_value(hadjustment, gtk_adjustment_get_value(hadjustment) + dx);
                gtk_adjustment_set_value(vadjustment, gtk_adjustment_get_value(vadjustment) + dy);
            }
        }

    }
    return FALSE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    MonitorData *data = (MonitorData *)user_data;
    if (data->actual_size && (event->state & GDK_BUTTON1_MASK)) {
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(data->scrolled_window));
        if (child != NULL) {
            GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(data->scrolled_window));
            GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(data->scrolled_window));
            gtk_adjustment_set_value(hadjustment, gtk_adjustment_get_value(hadjustment) - event->x);
            gtk_adjustment_set_value(vadjustment, gtk_adjustment_get_value(vadjustment) - event->y);
        }
    }
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    MonitorData *data = (MonitorData *)user_data;
    if (data->actual_size && event->button == 1) {
        gtk_widget_grab_focus(widget);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    MonitorData *data = (MonitorData *)user_data;
    if (data->actual_size && event->button == 1) {
        gtk_widget_grab_focus(widget);
        return TRUE;
    }
    return FALSE;
}


static void activate(GtkApplication *app, gpointer user_data) {
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) {
        fprintf(stderr, "Unable to open display\n");
        return;
    }

    char *path = NULL;

    if (global_argv[1]) {
        path = g_strdup((const char *)global_argv[1]);
    } else {
        const char *userprofile = g_getenv("USERPROFILE");
        path = g_build_filename(userprofile, "Pictures", NULL);
    }

    GFile *file = g_file_new_for_path(path);
    if (g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY) {
        images = get_image_files(path);
    } else if (has_image_extension(path)) {
        char *directory = g_path_get_dirname(path);
        images = get_image_files(directory);
        g_free(directory);

        // Set the specified file as the current image if it exists in the list
        GList *file_node = g_list_find_custom(images, path, (GCompareFunc)g_strcmp0);
        if (file_node) {
            current_image = file_node;
            // If there is more than one image, set the previous image as the current image
            if (g_list_length(images) > 1) {
                current_image = g_list_previous(current_image);
                if (current_image == NULL) {
                    current_image = g_list_last(images);
                }
            }
        }
    }
    g_object_unref(file);

    if (images == NULL) {
        fprintf(stderr, "No images found in the specified folder\n");
        g_free(path);
        //return;
    }

    num_monitors = gdk_display_get_n_monitors(display);
    monitor_data = g_new0(MonitorData, num_monitors);

    for (int i = 0; i < num_monitors; i++) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);

        GtkWindow *window = GTK_WINDOW(gtk_application_window_new(app));
        gtk_window_set_default_size(window, geometry.width, geometry.height);
        gtk_window_move(window, geometry.x, geometry.y);
        gtk_window_fullscreen(window);

        GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
        gtk_container_add(GTK_CONTAINER(window), scrolled_window);

        monitor_data[i].monitor = monitor;
        monitor_data[i].window = window;
        monitor_data[i].scrolled_window = scrolled_window;
        monitor_data[i].shrink_to_fit = TRUE;
        monitor_data[i].slideshow_active = TRUE;
        monitor_data[i].is_fullscreen = TRUE;
        monitor_data[i].actual_size = FALSE;
        monitor_data[i].options_visible = FALSE;
        monitor_data[i].timeout_id = 0;
        monitor_data[i].width = geometry.width;
        monitor_data[i].height = geometry.height;

        create_options_window(&monitor_data[i]);

        g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), &monitor_data[i]);
        g_signal_connect(window, "motion-notify-event", G_CALLBACK(on_motion_notify), &monitor_data[i]);
        g_signal_connect(window, "button-press-event", G_CALLBACK(on_button_press), &monitor_data[i]);
        g_signal_connect(window, "button-release-event", G_CALLBACK(on_button_release), &monitor_data[i]);

        gtk_widget_add_events(GTK_WIDGET(window), GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

        gtk_widget_show_all(GTK_WIDGET(window));
    }

    if(images){
        restart_slideshow();
        show_image_by_direction(TRUE);
    }

    g_free(path);
}

static int command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    int argc;
#ifdef DEBUG
    int i;
#endif
    global_argv = g_application_command_line_get_arguments(cmdline, &argc);
#ifdef DEBUG
    g_application_command_line_print (cmdline,
                                    "This text is written back\n"
                                    "to stdout of the caller\n");

    for (i = 0; i < argc; i++)
        g_print ("argument %d: %s\n", i, global_argv[i]);
#endif
    g_application_activate(app);

    return 0;
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.example.MonitorSlideshow", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(command_line), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Touchscreen pinch gestures for display and oscilloscope views.
 * RU: Жест щипка для дисплеев и осциллографов на сенсорном экране.
 */

/* EN: Claim both touches on gesture and cancel any pending mouse drag in view.
 * RU: Захватывает касания gesture и отменяет перетаскивание мышью в view. */
static void capture_pinch_begin(GtkGesture *gesture, GdkEventSequence *sequence, CaptureView *view)
{
    (void)sequence;
    view->pinching = TRUE;
    view->pinch_scale = 1;
    view->selecting = FALSE;
    view->pan_button = 0;
    gtk_gesture_set_state(gesture, GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_queue_draw(view->canvas);
}

/* EN: Apply the incremental pinch scale of gesture around its center in view.
 * RU: Применяет приращение scale жеста gesture вокруг его центра в view. */
static void capture_pinch_scale(GtkGestureZoom *gesture, gdouble scale, CaptureView *view)
{
    gdouble x, y;
    if (!view->pinching || scale <= 0 ||
        !gtk_gesture_get_bounding_box_center(GTK_GESTURE(gesture), &x, &y))
        return;
    gint width = MAX(1, gtk_widget_get_allocated_width(view->canvas));
    gint height = MAX(1, gtk_widget_get_allocated_height(view->canvas));
    capture_zoom_at(view, view->pinch_scale / scale, CLAMP(x / width, 0, 1), CLAMP(y / height, 0, 1));
    view->pinch_scale = scale;
}

/* EN: Mark view's pinch as ended when gesture releases sequence.
 * RU: Завершает щипок view, когда gesture отпускает sequence. */
static void capture_pinch_end(GtkGesture *gesture, GdkEventSequence *sequence, CaptureView *view)
{
    (void)gesture;
    (void)sequence;
    view->pinching = FALSE;
}


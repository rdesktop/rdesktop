void cliprdr_handle_SelectionNotify(XSelectionEvent * event);
void cliprdr_handle_SelectionRequest(XSelectionRequestEvent * xevent);
void xwin_register_propertynotify(Window event_wnd, Atom atom,
				  void (*propertycallback) (XPropertyEvent *));
void xwin_deregister_propertynotify(Window event_wnd, Atom atom);

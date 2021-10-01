# No-crash guarantee

The engine *must not* crash under any legal sequence of player inputs. In particular, this means that using a "high-watermark" static allocation of memory
is unacceptable if there is a way for player action to exceed that watermark and trigger a termination.

# UI Host

The main thread is taken over as message-pump thread for the UI host window, which serves as the swap chain target, and serves input and size change events.
In general, Win32 enforces that most calls against a window be done from its host thread (i.e., `DestroyWindow()`); however, we don't really plan on doing many calls against the window.
The semantics of the host thread as enforced by Win32 is that as soon as the host thread exits, the process exits; all threads are terminated, resources freed, etc.. Hence the UI host
must notify the other threads of an exit request (in response to `WM_CLOSE`). Importantly, the UI host cannot call `PostQuitMessage()` or otherwise stop pumping messages until the other
threads have exited, as calls against the window could block forever in that case. Hence the host must not only notify the others of a requested exit, it must also be notified of when an exit
is required (usually after the rest of the engine shuts down).

Note that we have no need for cleaning up after ourselves with the window handle or window class. These are resources created by the main thread, and when this exits, the process exits and the
resources are freed. Were we to regularly create and destroy classes / windows, then owning handles would be needed.

Unfortunately, we cannot easily do an asynchronous resize! At least not on separate threads. The reason is that there is a race during the fullscreen transition: the transition could occur
between the check of the `size_dirtied` event, and the call to `present()`.

It appears that we cannot defer handling of the `WM_SIZE` message; these are sent as part of a modal loop, and frame processing will block until it has finished.

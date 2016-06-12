# include "thread_index_activatable.h"
# include <gtk/gtk.h>

/**
 * SECTION: astroid_activatable
 * @short_description: Interface for activatable extensions on the shell
 * @see_also: #PeasExtensionSet
 *
 * #AstroidActivatable is an interface which should be implemented by
 * extensions that should be activated on the Liferea main window.
 **/

G_DEFINE_INTERFACE (AstroidThreadIndexActivatable, astroid_threadindex_activatable, G_TYPE_OBJECT)

void
astroid_threadindex_activatable_default_init (AstroidThreadIndexActivatableInterface *iface)
{
	static gboolean initialized = FALSE;

	if (!initialized) {
    g_printf ("initializing astroid threadindex activatable..\n");
		/**
		 * AstroidActivatable:window:
		 *
		 * The window property contains the gtr window for this
		 * #AstroidActivatable instance.
		 */
		g_object_interface_install_property (iface,
                           g_param_spec_object ("listview",
                                                "listview",
                                                "The ThreadIndex listview",
                                                GTK_TYPE_TREE_VIEW,
                                                G_PARAM_READWRITE |
                                                G_PARAM_CONSTRUCT_ONLY |
                                                G_PARAM_STATIC_STRINGS));

		initialized = TRUE;
	}
}

/**
 * astroid_activatable_activate:
 * @activatable: A #AstroidActivatable.
 *
 * Activates the extension on the shell property.
 */
void
astroid_threadindex_activatable_activate (AstroidThreadIndexActivatable * activatable)
{
	AstroidThreadIndexActivatableInterface *iface;

	g_return_if_fail (ASTROID_IS_THREADINDEX_ACTIVATABLE (activatable));

	iface = ASTROID_THREADINDEX_ACTIVATABLE_GET_IFACE (activatable);
	if (iface->activate)
		iface->activate (activatable);
}

/**
 * astroid_activatable_deactivate:
 * @activatable: A #AstroidActivatable.
 *
 * Deactivates the extension on the shell property.
 */
void
astroid_threadindex_activatable_deactivate (AstroidThreadIndexActivatable * activatable)
{
	AstroidThreadIndexActivatableInterface *iface;

	g_return_if_fail (ASTROID_IS_THREADINDEX_ACTIVATABLE (activatable));

	iface = ASTROID_THREADINDEX_ACTIVATABLE_GET_IFACE (activatable);
	if (iface->deactivate)
		iface->deactivate (activatable);
}

/**
 * astroid_activatable_update_state:
 * @activatable: A #AstroidActivatable.
 *
 * Triggers an update of the extension internal state to take into account
 * state changes in the window, due to some event or user action.
 */
void
astroid_threadindex_activatable_update_state (AstroidThreadIndexActivatable * activatable)
{
	AstroidThreadIndexActivatableInterface *iface;

	g_return_if_fail (ASTROID_IS_THREADINDEX_ACTIVATABLE (activatable));

	iface = ASTROID_THREADINDEX_ACTIVATABLE_GET_IFACE (activatable);
	if (iface->update_state)
		iface->update_state (activatable);
}

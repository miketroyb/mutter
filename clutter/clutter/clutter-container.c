/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 * ClutterContainer: Generic actor container interface.
 * Author: Emmanuele Bassi <ebassi@openedhand.com>
 */

#include "clutter/clutter-build-config.h"

#include <stdarg.h>
#include <glib-object.h>
#include <gobject/gvaluecollector.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "clutter/deprecated/clutter-container.h"

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-child-meta.h"
#include "clutter/clutter-container-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-enum-types.h"

#define CLUTTER_CONTAINER_WARN_NOT_IMPLEMENTED(container,vfunc) \
        G_STMT_START { \
          g_warning ("Container of type '%s' does not implement " \
                     "the required ClutterContainer::%s virtual " \
                     "function.",                                 \
                     G_OBJECT_TYPE_NAME ((container)),            \
                     (vfunc));                                    \
        } G_STMT_END

#define CLUTTER_CONTAINER_NOTE_NOT_IMPLEMENTED(container,vfunc) \
        G_STMT_START { \
          CLUTTER_NOTE (ACTOR, "Container of type '%s' does not "    \
                               "implement the ClutterContainer::%s " \
                               "virtual function.",                  \
                        G_OBJECT_TYPE_NAME ((container)),            \
                        (vfunc));                                    \
        } G_STMT_END

/**
 * ClutterContainer:
 * 
 * An interface for container actors
 *
 * #ClutterContainer is an interface implemented by [class@Actor], and
 * it provides some common API for notifying when a child actor is added
 * or removed, as well as the infrastructure for accessing child properties
 * through [class@ChildMeta].
 */

enum
{
  ACTOR_ADDED,
  ACTOR_REMOVED,
  CHILD_NOTIFY,

  LAST_SIGNAL
};

static guint container_signals[LAST_SIGNAL] = { 0, };
static GQuark quark_child_meta = 0;

static ClutterChildMeta *get_child_meta     (ClutterContainer *container,
                                             ClutterActor     *actor);
static void              create_child_meta  (ClutterContainer *container,
                                             ClutterActor     *actor);
static void              destroy_child_meta (ClutterContainer *container,
                                             ClutterActor     *actor);
static void              child_notify       (ClutterContainer *container,
                                             ClutterActor     *child,
                                             GParamSpec       *pspec);

typedef ClutterContainerIface   ClutterContainerInterface;

G_DEFINE_INTERFACE (ClutterContainer, clutter_container, G_TYPE_OBJECT);

static void
container_real_add (ClutterContainer *container,
                    ClutterActor     *actor)
{
  clutter_actor_add_child (CLUTTER_ACTOR (container), actor);
}

static void
container_real_remove (ClutterContainer *container,
                       ClutterActor     *actor)
{
  clutter_actor_remove_child (CLUTTER_ACTOR (container), actor);
}

static void
clutter_container_default_init (ClutterContainerInterface *iface)
{
  GType iface_type = G_TYPE_FROM_INTERFACE (iface);

  quark_child_meta =
    g_quark_from_static_string ("clutter-container-child-data");

  /**
   * ClutterContainer::actor-added:
   * @container: the actor which received the signal
   * @actor: the new child that has been added to @container
   *
   * The signal is emitted each time an actor
   * has been added to @container.
   */
  container_signals[ACTOR_ADDED] =
    g_signal_new (I_("actor-added"),
                  iface_type,
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (ClutterContainerIface, actor_added),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);
  /**
   * ClutterContainer::actor-removed:
   * @container: the actor which received the signal
   * @actor: the child that has been removed from @container
   *
   * The signal is emitted each time an actor
   * is removed from @container.
   */
  container_signals[ACTOR_REMOVED] =
    g_signal_new (I_("actor-removed"),
                  iface_type,
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (ClutterContainerIface, actor_removed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);

  /**
   * ClutterContainer::child-notify:
   * @container: the container which received the signal
   * @actor: the child that has had a property set
   * @pspec: (type GParamSpec): the #GParamSpec of the property set
   *
   * The signal is emitted each time a property is
   * being set through the clutter_container_child_set() and
   * clutter_container_child_set_property() calls.
   */
  container_signals[CHILD_NOTIFY] =
    g_signal_new (I_("child-notify"),
                  iface_type,
                  G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED,
                  G_STRUCT_OFFSET (ClutterContainerIface, child_notify),
                  NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_PARAM,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_ACTOR, G_TYPE_PARAM);

  iface->add = container_real_add;
  iface->remove = container_real_remove;

  iface->child_meta_type = G_TYPE_INVALID;
  iface->create_child_meta = create_child_meta;
  iface->destroy_child_meta = destroy_child_meta;
  iface->get_child_meta = get_child_meta;
  iface->child_notify = child_notify;
}

static inline void
container_add_actor (ClutterContainer *container,
                     ClutterActor     *actor)
{
  ClutterActor *parent;

  parent = clutter_actor_get_parent (actor);
  if (G_UNLIKELY (parent != NULL))
    {
      g_warning ("Attempting to add actor of type '%s' to a "
		 "container of type '%s', but the actor has "
                 "already a parent of type '%s'.",
		 g_type_name (G_OBJECT_TYPE (actor)),
		 g_type_name (G_OBJECT_TYPE (container)),
		 g_type_name (G_OBJECT_TYPE (parent)));
      return;
    }

  clutter_container_create_child_meta (container, actor);

#ifdef CLUTTER_ENABLE_DEBUG
  if (G_UNLIKELY (_clutter_diagnostic_enabled ()))
    {
      ClutterContainerIface *iface = CLUTTER_CONTAINER_GET_IFACE (container);

      if (iface->add != container_real_add)
        _clutter_diagnostic_message ("The ClutterContainer::add() virtual "
                                     "function has been deprecated and it "
                                     "should not be overridden by newly "
                                     "written code");
    }
#endif /* CLUTTER_ENABLE_DEBUG */

  CLUTTER_CONTAINER_GET_IFACE (container)->add (container, actor);
}

static inline void
container_remove_actor (ClutterContainer *container,
                        ClutterActor     *actor)
{
  ClutterActor *parent;

  parent = clutter_actor_get_parent (actor);
  if (parent != CLUTTER_ACTOR (container))
    {
      g_warning ("Attempting to remove actor of type '%s' from "
		 "group of class '%s', but the container is not "
                 "the actor's parent.",
		 g_type_name (G_OBJECT_TYPE (actor)),
		 g_type_name (G_OBJECT_TYPE (container)));
      return;
    }

  clutter_container_destroy_child_meta (container, actor);

#ifdef CLUTTER_ENABLE_DEBUG
  if (G_UNLIKELY (_clutter_diagnostic_enabled ()))
    {
      ClutterContainerIface *iface = CLUTTER_CONTAINER_GET_IFACE (container);

      if (iface->remove != container_real_remove)
        _clutter_diagnostic_message ("The ClutterContainer::remove() virtual "
                                     "function has been deprecated and it "
                                     "should not be overridden by newly "
                                     "written code");
    }
#endif /* CLUTTER_ENABLE_DEBUG */

  CLUTTER_CONTAINER_GET_IFACE (container)->remove (container, actor);
}

static inline void
container_add_valist (ClutterContainer *container,
                      ClutterActor     *first_actor,
                      va_list           args)
{
  ClutterActor *actor = first_actor;

  while (actor != NULL)
    {
      container_add_actor (container, actor);
      actor = va_arg (args, ClutterActor *);
    }
}

static inline void
container_remove_valist (ClutterContainer *container,
                         ClutterActor     *first_actor,
                         va_list           args)
{
  ClutterActor *actor = first_actor;

  while (actor != NULL)
    {
      container_remove_actor (container, actor);
      actor = va_arg (args, ClutterActor *);
    }
}

/**
 * clutter_container_add: (skip)
 * @container: a #ClutterContainer
 * @first_actor: the first #ClutterActor to add
 * @...: %NULL terminated list of actors to add
 *
 * Adds a list of `ClutterActor`s to @container. Each time and
 * actor is added, the "actor-added" signal is emitted. Each actor should
 * be parented to @container, which takes a reference on the actor. You
 * cannot add a #ClutterActor to more than one #ClutterContainer.
 *
 * This function will call #ClutterContainerIface.add(), which is a
 * deprecated virtual function. The default implementation will
 * call clutter_actor_add_child().
 *
 * Deprecated: 1.10: Use clutter_actor_add_child() instead.
 */
void
clutter_container_add (ClutterContainer *container,
                       ClutterActor     *first_actor,
                       ...)
{
  va_list args;

  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (first_actor));

  va_start (args, first_actor);
  container_add_valist (container, first_actor, args);
  va_end (args);
}

/**
 * clutter_container_add_actor: (virtual add)
 * @container: a #ClutterContainer
 * @actor: the first #ClutterActor to add
 *
 * Adds a #ClutterActor to @container. This function will emit the
 * "actor-added" signal. The actor should be parented to
 * @container. You cannot add a #ClutterActor to more than one
 * #ClutterContainer.
 *
 * This function will call #ClutterContainerIface.add(), which is a
 * deprecated virtual function. The default implementation will
 * call clutter_actor_add_child().
 *
 * Deprecated: 1.10: Use clutter_actor_add_child() instead.
 */
void
clutter_container_add_actor (ClutterContainer *container,
                             ClutterActor     *actor)
{
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  container_add_actor (container, actor);
}

/**
 * clutter_container_remove: (skip)
 * @container: a #ClutterContainer
 * @first_actor: first #ClutterActor to remove
 * @...: a %NULL-terminated list of actors to remove
 *
 * Removes a %NULL terminated list of `ClutterActor`s from
 * @container. Each actor should be unparented, so if you want to keep it
 * around you must hold a reference to it yourself, using g_object_ref().
 * Each time an actor is removed, the "actor-removed" signal is
 * emitted by @container.
 *
 * This function will call #ClutterContainerIface.remove(), which is a
 * deprecated virtual function. The default implementation will call
 * clutter_actor_remove_child().
 *
 * Deprecated: 1.10: Use clutter_actor_remove_child() instead.
 */
void
clutter_container_remove (ClutterContainer *container,
                          ClutterActor     *first_actor,
                          ...)
{
  va_list var_args;

  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (first_actor));

  va_start (var_args, first_actor);
  container_remove_valist (container, first_actor, var_args);
  va_end (var_args);
}

/**
 * clutter_container_remove_actor: (virtual remove)
 * @container: a #ClutterContainer
 * @actor: a #ClutterActor
 *
 * Removes @actor from @container. The actor should be unparented, so
 * if you want to keep it around you must hold a reference to it
 * yourself, using g_object_ref(). When the actor has been removed,
 * the "actor-removed" signal is emitted by @container.
 *
 * This function will call #ClutterContainerIface.remove(), which is a
 * deprecated virtual function. The default implementation will call
 * clutter_actor_remove_child().
 *
 * Deprecated: 1.10: Use clutter_actor_remove_child() instead.
 */
void
clutter_container_remove_actor (ClutterContainer *container,
                                ClutterActor     *actor)
{
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  container_remove_actor (container, actor);
}

/**
 * clutter_container_find_child_by_name:
 * @container: a #ClutterContainer
 * @child_name: the name of the requested child.
 *
 * Finds a child actor of a container by its name. Search recurses
 * into any child container.
 *
 * Return value: (transfer none): The child actor with the requested name,
 *   or %NULL if no actor with that name was found.
 */
ClutterActor *
clutter_container_find_child_by_name (ClutterContainer *container,
                                      const gchar      *child_name)
{
  GList        *children;
  GList        *iter;
  ClutterActor *actor = NULL;

  g_return_val_if_fail (CLUTTER_IS_CONTAINER (container), NULL);
  g_return_val_if_fail (child_name != NULL, NULL);

  children = clutter_actor_get_children (CLUTTER_ACTOR (container));

  for (iter = children; iter; iter = g_list_next (iter))
    {
      ClutterActor *a;
      const gchar  *iter_name;

      a = CLUTTER_ACTOR (iter->data);
      iter_name = clutter_actor_get_name (a);

      if (iter_name && !strcmp (iter_name, child_name))
        {
          actor = a;
          break;
        }

      if (CLUTTER_IS_CONTAINER (a))
        {
          ClutterContainer *c = CLUTTER_CONTAINER (a);

          actor = clutter_container_find_child_by_name (c, child_name);
          if (actor)
            break;
	}
    }

  g_list_free (children);

  return actor;
}

static ClutterChildMeta *
get_child_meta (ClutterContainer *container,
                ClutterActor     *actor)
{
  ClutterContainerIface *iface = CLUTTER_CONTAINER_GET_IFACE (container);
  ClutterChildMeta *meta;

  if (iface->child_meta_type == G_TYPE_INVALID)
    return NULL;

  meta = g_object_get_qdata (G_OBJECT (actor), quark_child_meta);
  if (meta != NULL && meta->actor == actor)
    return meta;

  return NULL;
}

static void
create_child_meta (ClutterContainer *container,
                   ClutterActor     *actor)
{
  ClutterContainerIface *iface = CLUTTER_CONTAINER_GET_IFACE (container);
  ClutterChildMeta *child_meta = NULL;

  if (iface->child_meta_type == G_TYPE_INVALID)
    return;

  if (!g_type_is_a (iface->child_meta_type, CLUTTER_TYPE_CHILD_META))
    {
      g_warning ("%s: Child data of type '%s' is not a ClutterChildMeta",
                 G_STRLOC, g_type_name (iface->child_meta_type));
      return;
    }

  child_meta = g_object_new (iface->child_meta_type,
                             "container", container,
                             "actor", actor,
                             NULL);

  g_object_set_qdata_full (G_OBJECT (actor), quark_child_meta,
                           child_meta,
                           (GDestroyNotify) g_object_unref);
}

static void
destroy_child_meta (ClutterContainer *container,
                    ClutterActor     *actor)
{
  ClutterContainerIface *iface  = CLUTTER_CONTAINER_GET_IFACE (container);

  if (iface->child_meta_type == G_TYPE_INVALID)
    return;

  g_object_set_qdata (G_OBJECT (actor), quark_child_meta, NULL);
}

/**
 * clutter_container_get_child_meta:
 * @container: a #ClutterContainer
 * @actor: a #ClutterActor that is a child of @container.
 *
 * Retrieves the #ClutterChildMeta which contains the data about the
 * @container specific state for @actor.
 *
 * Return value: (transfer none): the #ClutterChildMeta for the @actor child
 *   of @container or %NULL if the specific actor does not exist or the
 *   container is not configured to provide `ClutterChildMeta`s
 */
ClutterChildMeta *
clutter_container_get_child_meta (ClutterContainer *container,
                                  ClutterActor     *actor)
{
  ClutterContainerIface *iface = CLUTTER_CONTAINER_GET_IFACE (container);

  if (iface->child_meta_type == G_TYPE_INVALID)
    return NULL;

  if (G_LIKELY (iface->get_child_meta))
    return iface->get_child_meta (container, actor);

  return NULL;
}

/**
 * clutter_container_create_child_meta:
 * @container: a #ClutterContainer
 * @actor: a #ClutterActor
 *
 * Creates the #ClutterChildMeta wrapping @actor inside the
 * @container, if the #ClutterContainerIface::child_meta_type
 * class member is not set to %G_TYPE_INVALID.
 *
 * This function is only useful when adding a #ClutterActor to
 * a #ClutterContainer implementation outside of the
 * #ClutterContainer::add() virtual function implementation.
 *
 * Applications should not call this function.
 */
void
clutter_container_create_child_meta (ClutterContainer *container,
                                     ClutterActor     *actor)
{
  ClutterContainerIface *iface;

  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  iface = CLUTTER_CONTAINER_GET_IFACE (container);

  if (iface->child_meta_type == G_TYPE_INVALID)
    return;

  g_assert (g_type_is_a (iface->child_meta_type, CLUTTER_TYPE_CHILD_META));

  if (G_LIKELY (iface->create_child_meta))
    iface->create_child_meta (container, actor);
}

/**
 * clutter_container_destroy_child_meta:
 * @container: a #ClutterContainer
 * @actor: a #ClutterActor
 *
 * Destroys the #ClutterChildMeta wrapping @actor inside the
 * @container, if any.
 *
 * This function is only useful when removing a #ClutterActor to
 * a #ClutterContainer implementation outside of the
 * #ClutterContainer::add() virtual function implementation.
 *
 * Applications should not call this function.
 */
void
clutter_container_destroy_child_meta (ClutterContainer *container,
                                      ClutterActor     *actor)
{
  ClutterContainerIface *iface;

  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  iface = CLUTTER_CONTAINER_GET_IFACE (container);

  if (iface->child_meta_type == G_TYPE_INVALID)
    return;

  if (G_LIKELY (iface->destroy_child_meta))
    iface->destroy_child_meta (container, actor);
}

/**
 * clutter_container_class_find_child_property:
 * @klass: a #GObjectClass implementing the #ClutterContainer interface.
 * @property_name: a property name.
 *
 * Looks up the #GParamSpec for a child property of @klass.
 *
 * Return value: (transfer none): The #GParamSpec for the property or %NULL
 *   if no such property exist.
 */
GParamSpec *
clutter_container_class_find_child_property (GObjectClass *klass,
                                             const gchar  *property_name)
{
  ClutterContainerIface *iface;
  GObjectClass          *child_class;
  GParamSpec            *pspec;

  g_return_val_if_fail (G_IS_OBJECT_CLASS (klass), NULL);
  g_return_val_if_fail (property_name != NULL, NULL);
  g_return_val_if_fail (g_type_is_a (G_TYPE_FROM_CLASS (klass),
                                     CLUTTER_TYPE_CONTAINER),
                        NULL);

  iface = g_type_interface_peek (klass, CLUTTER_TYPE_CONTAINER);
  g_return_val_if_fail (iface != NULL, NULL);

  if (iface->child_meta_type == G_TYPE_INVALID)
    return NULL;

  child_class = g_type_class_ref (iface->child_meta_type);
  pspec = g_object_class_find_property (child_class, property_name);
  g_type_class_unref (child_class);

  return pspec;
}

/**
 * clutter_container_class_list_child_properties:
 * @klass: a #GObjectClass implementing the #ClutterContainer interface.
 * @n_properties: return location for length of returned array.
 *
 * Returns an array of #GParamSpec for all child properties.
 *
 * Return value: (array length=n_properties) (transfer full): an array
 *   of `GParamSpec`s which should be freed after use.
 */
GParamSpec **
clutter_container_class_list_child_properties (GObjectClass *klass,
                                               guint        *n_properties)
{
  ClutterContainerIface *iface;
  GObjectClass          *child_class;
  GParamSpec           **retval;

  g_return_val_if_fail (G_IS_OBJECT_CLASS (klass), NULL);
  g_return_val_if_fail (g_type_is_a (G_TYPE_FROM_CLASS (klass),
                                     CLUTTER_TYPE_CONTAINER),
                        NULL);

  iface = g_type_interface_peek (klass, CLUTTER_TYPE_CONTAINER);
  g_return_val_if_fail (iface != NULL, NULL);

  if (iface->child_meta_type == G_TYPE_INVALID)
    return NULL;

  child_class = g_type_class_ref (iface->child_meta_type);
  retval = g_object_class_list_properties (child_class, n_properties);
  g_type_class_unref (child_class);

  return retval;
}

static void
child_notify (ClutterContainer *container,
              ClutterActor     *actor,
              GParamSpec       *pspec)
{
}

static inline void
container_set_child_property (ClutterContainer *container,
                              ClutterActor     *actor,
                              const GValue     *value,
                              GParamSpec       *pspec)
{
  ClutterChildMeta *data;

  data = clutter_container_get_child_meta (container, actor);
  g_object_set_property (G_OBJECT (data), pspec->name, value);

  g_signal_emit (container, container_signals[CHILD_NOTIFY],
                 (pspec->flags & G_PARAM_STATIC_NAME)
                   ? g_quark_from_static_string (pspec->name)
                   : g_quark_from_string (pspec->name),
                 actor, pspec);
}

/**
 * clutter_container_child_set_property:
 * @container: a #ClutterContainer
 * @child: a #ClutterActor that is a child of @container.
 * @property: the name of the property to set.
 * @value: the value.
 *
 * Sets a container-specific property on a child of @container.
 */
void
clutter_container_child_set_property (ClutterContainer *container,
                                      ClutterActor     *child,
                                      const gchar      *property,
                                      const GValue     *value)
{
  GObjectClass *klass;
  GParamSpec   *pspec;

  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (property != NULL);
  g_return_if_fail (value != NULL);

  klass = G_OBJECT_GET_CLASS (container);

  pspec = clutter_container_class_find_child_property (klass, property);
  if (!pspec)
    {
      g_warning ("%s: Containers of type '%s' have no child "
                 "property named '%s'",
                 G_STRLOC, G_OBJECT_TYPE_NAME (container), property);
      return;
    }

  if (!(pspec->flags & G_PARAM_WRITABLE))
    {
      g_warning ("%s: Child property '%s' of the container '%s' "
                 "is not writable",
                 G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (container));
      return;
    }

  container_set_child_property (container, child, value, pspec);
}

/**
 * clutter_container_child_set:
 * @container: a #ClutterContainer
 * @actor: a #ClutterActor that is a child of @container.
 * @first_prop: name of the first property to be set.
 * @...: value for the first property, followed optionally by more name/value
 * pairs terminated with NULL.
 *
 * Sets container specific properties on the child of a container.
 */
void
clutter_container_child_set (ClutterContainer *container,
                             ClutterActor     *actor,
                             const gchar      *first_prop,
                             ...)
{
  GObjectClass *klass;
  const gchar *name;
  va_list var_args;
  
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  klass = G_OBJECT_GET_CLASS (container);

  va_start (var_args, first_prop);

  name = first_prop;
  while (name)
    {
      GValue value = G_VALUE_INIT;
      gchar *error = NULL;
      GParamSpec *pspec;
    
      pspec = clutter_container_class_find_child_property (klass, name);
      if (!pspec)
        {
          g_warning ("%s: Containers of type '%s' have no child "
                     "property named '%s'",
                     G_STRLOC, G_OBJECT_TYPE_NAME (container), name);
          break;
        }

      if (!(pspec->flags & G_PARAM_WRITABLE))
        {
          g_warning ("%s: Child property '%s' of the container '%s' "
                     "is not writable",
                     G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (container));
          break;
        }

      G_VALUE_COLLECT_INIT (&value, G_PARAM_SPEC_VALUE_TYPE (pspec),
                            var_args, 0,
                            &error);

      if (error)
        {
          /* we intentionally leak the GValue because it might
           * be in an undefined state and calling g_value_unset()
           * on it might crash
           */
          g_warning ("%s: %s", G_STRLOC, error);
          g_free (error);
          break;
        }

      container_set_child_property (container, actor, &value, pspec);

      g_value_unset (&value);

      name = va_arg (var_args, gchar*);
    }

  va_end (var_args);
}

static inline void
container_get_child_property (ClutterContainer *container,
                              ClutterActor     *actor,
                              GValue           *value,
                              GParamSpec       *pspec)
{
  ClutterChildMeta *data;

  data = clutter_container_get_child_meta (container, actor);
  g_object_get_property (G_OBJECT (data), pspec->name, value);
}

/**
 * clutter_container_child_get_property:
 * @container: a #ClutterContainer
 * @child: a #ClutterActor that is a child of @container.
 * @property: the name of the property to set.
 * @value: the value.
 *
 * Gets a container specific property of a child of @container, In general,
 * a copy is made of the property contents and the caller is responsible for
 * freeing the memory by calling g_value_unset().
 *
 * Note that clutter_container_child_set_property() is really intended for
 * language bindings, clutter_container_child_set() is much more convenient
 * for C programming.
 */
void
clutter_container_child_get_property (ClutterContainer *container,
                                      ClutterActor     *child,
                                      const gchar      *property,
                                      GValue           *value)
{
  GObjectClass *klass;
  GParamSpec   *pspec;

  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (property != NULL);
  g_return_if_fail (value != NULL);

  klass = G_OBJECT_GET_CLASS (container);

  pspec = clutter_container_class_find_child_property (klass, property);
  if (!pspec)
    {
      g_warning ("%s: Containers of type '%s' have no child "
                 "property named '%s'",
                 G_STRLOC, G_OBJECT_TYPE_NAME (container), property);
      return;
    }

  if (!(pspec->flags & G_PARAM_READABLE))
    {
      g_warning ("%s: Child property '%s' of the container '%s' "
                 "is not writable",
                 G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (container));
      return;
    }

  container_get_child_property (container, child, value, pspec);
}


/**
 * clutter_container_child_get:
 * @container: a #ClutterContainer
 * @actor: a #ClutterActor that is a child of @container.
 * @first_prop: name of the first property to be set.
 * @...: value for the first property, followed optionally by more name/value
 * pairs terminated with NULL.
 *
 * Gets @container specific properties of an actor.
 *
 * In general, a copy is made of the property contents and the caller is
 * responsible for freeing the memory in the appropriate manner for the type, for
 * instance by calling g_free() or g_object_unref(). 
 */
void
clutter_container_child_get (ClutterContainer *container,
                             ClutterActor     *actor,
                             const gchar      *first_prop,
                             ...)
{
  GObjectClass *klass;
  const gchar *name;
  va_list var_args;
  
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  klass = G_OBJECT_GET_CLASS (container);

  va_start (var_args, first_prop);

  name = first_prop;
  while (name)
    {
      GValue value = G_VALUE_INIT;
      gchar *error = NULL;
      GParamSpec *pspec;
    
      pspec = clutter_container_class_find_child_property (klass, name);
      if (!pspec)
        {
          g_warning ("%s: container '%s' has no child property named '%s'",
                     G_STRLOC, G_OBJECT_TYPE_NAME (container), name);
          break;
        }

      if (!(pspec->flags & G_PARAM_READABLE))
        {
          g_warning ("%s: child property '%s' of container '%s' is not readable",
                     G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (container));
          break;
        }

      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));

      container_get_child_property (container, actor, &value, pspec);

      G_VALUE_LCOPY (&value, var_args, 0, &error);
      if (error)
        {
          g_warning ("%s: %s", G_STRLOC, error);
          g_free (error);
          g_value_unset (&value);
          break;
        }

      g_value_unset (&value);

      name = va_arg (var_args, gchar*);
    }

  va_end (var_args);
}

/**
 * clutter_container_child_notify:
 * @container: a #ClutterContainer
 * @child: a #ClutterActor
 * @pspec: a #GParamSpec
 *
 * Calls the #ClutterContainerIface.child_notify() virtual function
 * of #ClutterContainer. The default implementation will emit the
 * #ClutterContainer::child-notify signal.
 */
void
clutter_container_child_notify (ClutterContainer *container,
                                ClutterActor     *child,
                                GParamSpec       *pspec)
{
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (pspec != NULL);

  g_return_if_fail (clutter_actor_get_parent (child) == CLUTTER_ACTOR (container));

  CLUTTER_CONTAINER_GET_IFACE (container)->child_notify (container,
                                                         child,
                                                         pspec);
}

void
_clutter_container_emit_actor_added (ClutterContainer *container,
                                     ClutterActor     *actor)
{
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  g_signal_emit (container, container_signals[ACTOR_ADDED], 0, actor);
}

void
_clutter_container_emit_actor_removed (ClutterContainer *container,
                                       ClutterActor     *actor)
{
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  g_signal_emit (container, container_signals[ACTOR_REMOVED], 0, actor);
}

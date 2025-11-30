#!/usr/bin/env python3
"""
Blender script to render turntable preview frames.
Renders rotation previews around the largest objects in the scene sequentially.

Run with: blender --background --python turntable_render.py -- input.blend output_dir frame_count resolution
"""

import bpy
import sys
import os
import math
from mathutils import Vector

# Maximum number of objects to feature in the preview
MAX_FEATURED_OBJECTS = 5


def ensure_object_mode():
    """Ensure we're in object mode for operator calls."""
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')


def get_object_bounds(obj):
    """Calculate the world-space bounding box center and size for a single object."""
    try:
        if obj.type != 'MESH':
            return None, 0.0

        # Check if mesh has geometry
        if not obj.data or len(obj.data.vertices) == 0:
            return None, 0.0

        min_co = Vector((float('inf'), float('inf'), float('inf')))
        max_co = Vector((float('-inf'), float('-inf'), float('-inf')))

        for corner in obj.bound_box:
            world_corner = obj.matrix_world @ Vector(corner)
            min_co.x = min(min_co.x, world_corner.x)
            min_co.y = min(min_co.y, world_corner.y)
            min_co.z = min(min_co.z, world_corner.z)
            max_co.x = max(max_co.x, world_corner.x)
            max_co.y = max(max_co.y, world_corner.y)
            max_co.z = max(max_co.z, world_corner.z)

        center = (min_co + max_co) / 2
        size = (max_co - min_co).length

        return center, max(size, 0.1)
    except Exception as e:
        print(f"Warning: Could not get bounds for {obj.name}: {e}")
        return None, 0.0


def get_largest_objects(max_count=MAX_FEATURED_OBJECTS):
    """
    Find the largest mesh objects in the scene.
    Returns a list of tuples: (object, center, size) sorted by size descending.
    """
    objects_with_size = []

    for obj in bpy.context.scene.objects:
        if obj.type == 'MESH':
            center, size = get_object_bounds(obj)
            if center is not None and size > 0.01:  # Skip tiny objects
                objects_with_size.append((obj, center, size))

    # Sort by size, largest first
    objects_with_size.sort(key=lambda x: x[2], reverse=True)

    print(f"Found {len(objects_with_size)} mesh objects")
    for i, (obj, center, size) in enumerate(objects_with_size[:max_count]):
        print(f"  {i+1}. {obj.name}: size={size:.2f}")

    # Return top N
    return objects_with_size[:max_count]


def get_scene_bounds():
    """Calculate the bounding box of all mesh objects (fallback)."""
    min_co = Vector((float('inf'), float('inf'), float('inf')))
    max_co = Vector((float('-inf'), float('-inf'), float('-inf')))

    has_objects = False
    for obj in bpy.context.scene.objects:
        if obj.type == 'MESH':
            try:
                has_objects = True
                for corner in obj.bound_box:
                    world_corner = obj.matrix_world @ Vector(corner)
                    min_co.x = min(min_co.x, world_corner.x)
                    min_co.y = min(min_co.y, world_corner.y)
                    min_co.z = min(min_co.z, world_corner.z)
                    max_co.x = max(max_co.x, world_corner.x)
                    max_co.y = max(max_co.y, world_corner.y)
                    max_co.z = max(max_co.z, world_corner.z)
            except Exception as e:
                print(f"Warning: Could not process {obj.name}: {e}")

    if not has_objects:
        return Vector((0, 0, 0)), 1.0

    center = (min_co + max_co) / 2
    size = (max_co - min_co).length

    return center, max(size, 0.1)


def create_camera(location):
    """Create a camera using data API (works in background mode)."""
    cam_data = bpy.data.cameras.new(name="TurntableCamera")
    cam_obj = bpy.data.objects.new("TurntableCamera", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    cam_obj.location = location
    return cam_obj


def create_sun_light(location, energy=3.0):
    """Create a sun light using data API (works in background mode)."""
    light_data = bpy.data.lights.new(name="TurntableSun", type='SUN')
    light_data.energy = energy
    light_obj = bpy.data.objects.new("TurntableSun", light_data)
    bpy.context.scene.collection.objects.link(light_obj)
    light_obj.location = location
    return light_obj


def create_area_light(location, energy=100.0, size=1.0):
    """Create an area light using data API (works in background mode)."""
    light_data = bpy.data.lights.new(name="TurntableFill", type='AREA')
    light_data.energy = energy
    light_data.size = size
    light_obj = bpy.data.objects.new("TurntableFill", light_data)
    bpy.context.scene.collection.objects.link(light_obj)
    light_obj.location = location
    return light_obj


def create_empty(location):
    """Create an empty using data API (works in background mode)."""
    empty_obj = bpy.data.objects.new("CameraPivot", None)
    bpy.context.scene.collection.objects.link(empty_obj)
    empty_obj.location = location
    return empty_obj


def setup_camera(center, size):
    """Create and position camera to view an object."""
    distance = size * 2.5

    # Check if camera already exists
    camera = bpy.data.objects.get("TurntableCamera")
    if camera is None:
        camera = create_camera((distance, 0, distance * 0.5))

    bpy.context.scene.camera = camera
    return camera, distance


def setup_lighting(size):
    """Create lighting for the scene (only once)."""
    # Check if lights already exist
    if bpy.data.objects.get("TurntableSun"):
        return

    distance = size * 2.5

    # Create sun light
    create_sun_light((distance, distance, distance * 2), energy=3.0)

    # Create fill light
    create_area_light((-distance, -distance, distance), energy=100.0, size=size)

    print("Lighting created")


def setup_pivot(center):
    """Create or update the camera pivot point."""
    pivot = bpy.data.objects.get("CameraPivot")
    if pivot is None:
        pivot = create_empty(center)
    else:
        pivot.location = center
        pivot.rotation_euler = (0, 0, 0)

    return pivot


def position_camera_for_object(camera, pivot, center, size):
    """Position camera to frame a specific object."""
    distance = size * 2.5

    # Update pivot location
    pivot.location = center

    # Position camera relative to pivot
    camera.parent = pivot
    camera.location = Vector((distance, 0, distance * 0.5))

    # Point camera at pivot (center of object)
    direction = Vector((0, 0, 0)) - camera.location
    rot_quat = direction.to_track_quat('-Z', 'Y')
    camera.rotation_euler = rot_quat.to_euler()


def set_object_visibility(featured_objects, current_obj):
    """Hide all objects except the current featured object and non-mesh objects."""
    for obj in bpy.context.scene.objects:
        if obj.type == 'MESH':
            # Show only the current object
            obj.hide_render = (obj != current_obj)
            obj.hide_viewport = (obj != current_obj)
        elif obj.type in ('CAMERA', 'LIGHT', 'EMPTY'):
            # Always show cameras, lights, empties
            obj.hide_render = False
            obj.hide_viewport = False


def restore_visibility():
    """Restore visibility of all objects."""
    for obj in bpy.context.scene.objects:
        obj.hide_render = False
        obj.hide_viewport = False


def setup_render_settings(resolution, output_path, frame):
    """Configure render settings for thumbnail output."""
    scene = bpy.context.scene

    # Try to set EEVEE - use try/except for compatibility
    try:
        scene.render.engine = 'BLENDER_EEVEE_NEXT'
    except TypeError:
        scene.render.engine = 'BLENDER_EEVEE'

    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True

    # Output settings
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.filepath = os.path.join(output_path, f"frame_{frame:03d}.png")

    # Performance - fast preview quality
    if hasattr(scene, 'eevee'):
        scene.eevee.taa_render_samples = 16


def render_turntable(blend_file, output_dir, frame_count, resolution):
    """Main function to render turntable frames around largest objects."""
    print(f"Opening: {blend_file}")

    # Open the blend file
    try:
        bpy.ops.wm.open_mainfile(filepath=blend_file)
    except Exception as e:
        print(f"Error opening file: {e}")
        return False

    # Ensure we're in object mode
    ensure_object_mode()

    # Get largest objects
    featured_objects = get_largest_objects(MAX_FEATURED_OBJECTS)

    if not featured_objects:
        # Fallback: render whole scene if no suitable objects found
        print("No suitable mesh objects found, rendering whole scene")
        center, size = get_scene_bounds()
        featured_objects = [(None, center, size)]

    num_objects = len(featured_objects)
    frames_per_object = max(1, frame_count // num_objects)

    print(f"Rendering {num_objects} objects, {frames_per_object} frames each")

    # Setup lighting based on largest object
    _, _, largest_size = featured_objects[0]
    setup_lighting(largest_size)

    # Create camera and pivot
    camera, _ = setup_camera(Vector((0, 0, 0)), largest_size)
    pivot = setup_pivot(Vector((0, 0, 0)))

    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)

    frame_index = 0

    # Render each featured object
    for obj_index, (obj, center, size) in enumerate(featured_objects):
        obj_name = obj.name if obj else "Scene"
        print(f"Object {obj_index + 1}/{num_objects}: {obj_name} (size: {size:.2f})")

        # Hide other objects, show only current
        if obj is not None:
            set_object_visibility(featured_objects, obj)

        # Position camera for this object
        position_camera_for_object(camera, pivot, center, size)

        # Render rotation frames for this object
        for i in range(frames_per_object):
            # Rotate pivot
            angle = (2 * math.pi * i) / frames_per_object
            pivot.rotation_euler = (0, 0, angle)

            # Setup render output
            setup_render_settings(resolution, output_dir, frame_index)

            # Render
            try:
                bpy.ops.render.render(write_still=True)
                print(f"  Frame {frame_index + 1}/{frame_count}")
            except Exception as e:
                print(f"  Frame {frame_index + 1} render failed: {e}")

            frame_index += 1

            # Stop if we've rendered enough frames
            if frame_index >= frame_count:
                break

        if frame_index >= frame_count:
            break

    # Restore visibility
    restore_visibility()

    print(f"Complete: {frame_index} frames in {output_dir}")
    return True


if __name__ == "__main__":
    # Parse command line arguments after "--"
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        print("Usage: blender --background --python turntable_render.py -- input.blend output_dir frame_count resolution")
        sys.exit(1)

    if len(argv) < 4:
        print("Usage: blender --background --python turntable_render.py -- input.blend output_dir frame_count resolution")
        sys.exit(1)

    blend_file = argv[0]
    output_dir = argv[1]
    frame_count = int(argv[2])
    resolution = int(argv[3])

    success = render_turntable(blend_file, output_dir, frame_count, resolution)
    sys.exit(0 if success else 1)

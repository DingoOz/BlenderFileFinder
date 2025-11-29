#!/usr/bin/env python3
"""
Blender script to render turntable preview frames.
Run with: blender --background --python turntable_render.py -- input.blend output_dir frame_count resolution
"""

import bpy
import sys
import os
import math
from mathutils import Vector

def clear_scene():
    """Remove default objects."""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

def setup_camera_and_light(center, size):
    """Set up camera and lighting for the turntable."""
    # Calculate camera distance based on object size
    distance = size * 2.5

    # Create camera
    bpy.ops.object.camera_add(location=(distance, 0, distance * 0.5))
    camera = bpy.context.object
    camera.name = "TurntableCamera"

    # Point camera at center
    direction = Vector(center) - camera.location
    rot_quat = direction.to_track_quat('-Z', 'Y')
    camera.rotation_euler = rot_quat.to_euler()

    bpy.context.scene.camera = camera

    # Create sun light
    bpy.ops.object.light_add(type='SUN', location=(distance, distance, distance * 2))
    sun = bpy.context.object
    sun.data.energy = 3.0

    # Create fill light
    bpy.ops.object.light_add(type='AREA', location=(-distance, -distance, distance))
    fill = bpy.context.object
    fill.data.energy = 100.0
    fill.data.size = size

    return camera, distance

def setup_render_settings(resolution, output_path, frame):
    """Configure render settings for thumbnail output."""
    scene = bpy.context.scene

    # Render settings
    scene.render.engine = 'BLENDER_EEVEE_NEXT' if hasattr(bpy.types, 'EEVEE_NEXT') else 'BLENDER_EEVEE'
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True

    # Output settings
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.filepath = os.path.join(output_path, f"frame_{frame:03d}.png")

    # Performance - fast preview quality
    if scene.render.engine == 'BLENDER_EEVEE':
        scene.eevee.taa_render_samples = 16

def get_scene_bounds():
    """Calculate the bounding box of all mesh objects."""
    min_co = Vector((float('inf'), float('inf'), float('inf')))
    max_co = Vector((float('-inf'), float('-inf'), float('-inf')))

    has_objects = False
    for obj in bpy.context.scene.objects:
        if obj.type == 'MESH':
            has_objects = True
            for corner in obj.bound_box:
                world_corner = obj.matrix_world @ Vector(corner)
                min_co.x = min(min_co.x, world_corner.x)
                min_co.y = min(min_co.y, world_corner.y)
                min_co.z = min(min_co.z, world_corner.z)
                max_co.x = max(max_co.x, world_corner.x)
                max_co.y = max(max_co.y, world_corner.y)
                max_co.z = max(max_co.z, world_corner.z)

    if not has_objects:
        return Vector((0, 0, 0)), 1.0

    center = (min_co + max_co) / 2
    size = (max_co - min_co).length

    return center, max(size, 0.1)

def render_turntable(blend_file, output_dir, frame_count, resolution):
    """Main function to render turntable frames."""
    # Open the blend file
    bpy.ops.wm.open_mainfile(filepath=blend_file)

    # Get scene bounds
    center, size = get_scene_bounds()

    # Setup camera and lights
    camera, distance = setup_camera_and_light(center, size)

    # Create empty at center for camera to orbit around
    bpy.ops.object.empty_add(location=center)
    pivot = bpy.context.object
    pivot.name = "CameraPivot"

    # Parent camera to pivot
    camera.parent = pivot
    camera.location = Vector((distance, 0, distance * 0.5)) - Vector(center)

    # Point camera at center
    direction = Vector((0, 0, 0)) - camera.location
    rot_quat = direction.to_track_quat('-Z', 'Y')
    camera.rotation_euler = rot_quat.to_euler()

    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)

    # Render each frame
    for i in range(frame_count):
        # Rotate pivot
        angle = (2 * math.pi * i) / frame_count
        pivot.rotation_euler = (0, 0, angle)

        # Setup render output
        setup_render_settings(resolution, output_dir, i)

        # Render
        bpy.ops.render.render(write_still=True)

        print(f"Rendered frame {i + 1}/{frame_count}")

    print(f"Turntable render complete: {output_dir}")

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

    render_turntable(blend_file, output_dir, frame_count, resolution)

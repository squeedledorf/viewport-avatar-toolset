add_executable(vats_tests
    main.cpp
    test_anim.cpp
    test_avatar_mesh.cpp
    test_bvh.cpp
    test_curve_filter.cpp
    test_curve_ops.cpp
    test_motion_quality.cpp
    test_simplify.cpp
    test_dope_sheet.cpp
    test_motion_path.cpp
    test_dynamics.cpp
    test_idle.cpp
    test_overlap.cpp
    test_ortho.cpp
    test_sl_camera.cpp
    test_ragdoll.cpp
    test_face_anim.cpp
    test_expression_pack.cpp
    test_lip_sync.cpp
    test_groups.cpp
    test_dae.cpp
    test_bone_heat.cpp
    test_auto_rig.cpp
    test_weight_paint.cpp
    test_gltf_mesh.cpp
    test_rig_export.cpp
    test_shape_keys.cpp
    test_face_fit.cpp
    test_body_shape.cpp
    test_soft_body.cpp
    test_export_name.cpp
    test_sit_export.cpp
    test_clips.cpp
    test_library.cpp
    test_fcurve.cpp
    test_history.cpp
    test_math.cpp
    test_firewall.cpp
    test_wiki.cpp
    ${PROJECT_SOURCE_DIR}/ui/firewall.cpp
    test_footlock.cpp
    test_balance.cpp
    test_jump_arc.cpp
    test_reach.cpp
    test_gif.cpp
    test_lint.cpp
    test_deformer.cpp
    test_self_contact.cpp
    test_community_folder.cpp
    test_loop_tools.cpp
    test_loop_assist.cpp
    test_hostile_inputs.cpp
    test_mocap.cpp
    test_pose_match.cpp
    test_pose_ops.cpp
    test_pose_presets.cpp
    test_pose_tools.cpp
    test_project.cpp
    test_reference.cpp
    test_retarget.cpp
    test_batch_retarget.cpp
    test_priority_plan.cpp
    test_rig.cpp
    test_rig_axes.cpp
    test_rig_map.cpp
    test_spare_chains.cpp
    test_rig_map_humanoids.cpp
    test_rig_constraints.cpp
    test_shape.cpp
    test_skeleton.cpp
    test_fluid_pose.cpp
    test_sl_preview.cpp
    test_retime.cpp
    test_world_reduce.cpp
    test_height_variant.cpp
    test_time_edit.cpp
    test_tween.cpp
    test_key_tags.cpp
    test_selection_sets.cpp
    test_box_select.cpp
    test_picker.cpp
    test_widgets.cpp  # ui/widgets.h, header-only parts
    test_pie.cpp
    ${PROJECT_SOURCE_DIR}/ui/pie.cpp
)
target_link_libraries(vats_tests PRIVATE vats::core)
if(VATS_LEGACY_IMPORT)
    target_sources(vats_tests PRIVATE test_legacy_import.cpp)
endif()
if(TARGET imgui)  # the shortcut overrides use ImGui's key names (ui/keymap.h); the dock layout a headless context
    target_sources(vats_tests PRIVATE test_keymap.cpp ${PROJECT_SOURCE_DIR}/ui/keymap.cpp ${PROJECT_SOURCE_DIR}/ui/settings.cpp
                                      test_dock_layout.cpp ${PROJECT_SOURCE_DIR}/ui/dock_layout.cpp
                                      test_workspaces.cpp ${PROJECT_SOURCE_DIR}/ui/workspaces.cpp
                                      test_theme.cpp ${PROJECT_SOURCE_DIR}/ui/theme.cpp
                                      test_menu_crawl.cpp ${PROJECT_SOURCE_DIR}/ui/icon_button.cpp)
    target_link_libraries(vats_tests PRIVATE imgui)
endif()
if(VATS_FBX)
    target_sources(vats_tests PRIVATE test_fbx.cpp)
endif()
target_include_directories(vats_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/ui)
target_compile_definitions(vats_tests PRIVATE VATS_DATA_DIR="${PROJECT_SOURCE_DIR}/data/character"
                                               VATS_TEST_FILES="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                               VATS_WIKI_DIR="${PROJECT_SOURCE_DIR}/docs/wiki"
                                               VATS_ASSETS_DIR="${PROJECT_SOURCE_DIR}/app/assets")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # viewer_ref must compute exactly like the viewer, same as core/src/anim_file.cpp.
    target_compile_options(vats_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME vats_tests COMMAND vats_tests)


/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ExternalScrollId, PipelineId, PropertyBinding, ReferenceFrameKind, ScrollClamping, ScrollLocation};
use api::{TransformStyle, ScrollSensitivity, StickyOffsetBounds};
use api::units::*;
use clip_scroll_tree::{CoordinateSystem, CoordinateSystemId, SpatialNodeIndex, TransformUpdateState};
use euclid::SideOffsets2D;
use gpu_types::TransformPalette;
use scene::SceneProperties;
use util::{LayoutFastTransform, LayoutToWorldFastTransform, ScaleOffset, TransformedRectKind};

#[derive(Clone, Debug)]
pub enum SpatialNodeType {
    /// A special kind of node that adjusts its position based on the position
    /// of its parent node and a given set of sticky positioning offset bounds.
    /// Sticky positioned is described in the CSS Positioned Layout Module Level 3 here:
    /// https://www.w3.org/TR/css-position-3/#sticky-pos
    StickyFrame(StickyFrameInfo),

    /// Transforms it's content, but doesn't clip it. Can also be adjusted
    /// by scroll events or setting scroll offsets.
    ScrollFrame(ScrollFrameInfo),

    /// A reference frame establishes a new coordinate space in the tree.
    ReferenceFrame(ReferenceFrameInfo),
}

/// Contains information common among all types of ClipScrollTree nodes.
#[derive(Clone, Debug)]
pub struct SpatialNode {
    /// The transformation for this viewport in world coordinates is the transformation for
    /// our parent reference frame, plus any accumulated scrolling offsets from nodes
    /// between our reference frame and this node. For reference frames, we also include
    /// whatever local transformation this reference frame provides.
    pub world_viewport_transform: LayoutToWorldFastTransform,

    /// World transform for content transformed by this node.
    pub world_content_transform: LayoutToWorldFastTransform,

    /// The current transform kind of world_content_transform.
    pub transform_kind: TransformedRectKind,

    /// Pipeline that this layer belongs to
    pub pipeline_id: PipelineId,

    /// Parent layer. If this is None, we are the root node.
    pub parent: Option<SpatialNodeIndex>,

    /// Child layers
    pub children: Vec<SpatialNodeIndex>,

    /// The type of this node and any data associated with that node type.
    pub node_type: SpatialNodeType,

    /// True if this node is transformed by an invertible transform.  If not, display items
    /// transformed by this node will not be displayed and display items not transformed by this
    /// node will not be clipped by clips that are transformed by this node.
    pub invertible: bool,

    /// The axis-aligned coordinate system id of this node.
    pub coordinate_system_id: CoordinateSystemId,

    /// The transformation from the coordinate system which established our compatible coordinate
    /// system (same coordinate system id) and us. This can change via scroll offsets and via new
    /// reference frame transforms.
    pub coordinate_system_relative_scale_offset: ScaleOffset,
}

fn compute_offset_from(
    mut current: Option<SpatialNodeIndex>,
    external_id: ExternalScrollId,
    previous_spatial_nodes: &[SpatialNode],
) -> LayoutVector2D {
    let mut offset = LayoutVector2D::zero();
    while let Some(parent_index) = current {
        let ancestor = &previous_spatial_nodes[parent_index.0 as usize];
        match ancestor.node_type {
            SpatialNodeType::ReferenceFrame(..) => {
                // We don't want to scroll across reference frames.
                break;
            },
            SpatialNodeType::ScrollFrame(ref info) => {
                if info.external_id == Some(external_id) {
                    break;
                }

                // External scroll offsets are not propagated across
                // reference frame boundaries, so undo them here.
                offset += info.offset + info.external_scroll_offset;
            },
            SpatialNodeType::StickyFrame(ref info) => {
                offset += info.current_offset;
            },
        }
        current = ancestor.parent;
    }
    offset
}

impl SpatialNode {
    pub fn new(
        pipeline_id: PipelineId,
        parent_index: Option<SpatialNodeIndex>,
        node_type: SpatialNodeType,
    ) -> Self {
        SpatialNode {
            world_viewport_transform: LayoutToWorldFastTransform::identity(),
            world_content_transform: LayoutToWorldFastTransform::identity(),
            transform_kind: TransformedRectKind::AxisAligned,
            parent: parent_index,
            children: Vec::new(),
            pipeline_id,
            node_type,
            invertible: true,
            coordinate_system_id: CoordinateSystemId(0),
            coordinate_system_relative_scale_offset: ScaleOffset::identity(),
        }
    }

    pub fn new_scroll_frame(
        pipeline_id: PipelineId,
        parent_index: SpatialNodeIndex,
        external_id: Option<ExternalScrollId>,
        frame_rect: &LayoutRect,
        content_size: &LayoutSize,
        scroll_sensitivity: ScrollSensitivity,
        frame_kind: ScrollFrameKind,
        external_scroll_offset: LayoutVector2D,
    ) -> Self {
        let node_type = SpatialNodeType::ScrollFrame(ScrollFrameInfo::new(
                *frame_rect,
                scroll_sensitivity,
                LayoutSize::new(
                    (content_size.width - frame_rect.size.width).max(0.0),
                    (content_size.height - frame_rect.size.height).max(0.0)
                ),
                external_id,
                frame_kind,
                external_scroll_offset,
            )
        );

        Self::new(pipeline_id, Some(parent_index), node_type)
    }

    pub fn new_reference_frame(
        parent_index: Option<SpatialNodeIndex>,
        transform_style: TransformStyle,
        source_transform: PropertyBinding<LayoutTransform>,
        kind: ReferenceFrameKind,
        origin_in_parent_reference_frame: LayoutVector2D,
        pipeline_id: PipelineId,
    ) -> Self {
        let info = ReferenceFrameInfo {
            transform_style,
            source_transform,
            kind,
            origin_in_parent_reference_frame,
            invertible: true,
        };
        Self::new(pipeline_id, parent_index, SpatialNodeType::ReferenceFrame(info))
    }

    pub fn new_sticky_frame(
        parent_index: SpatialNodeIndex,
        sticky_frame_info: StickyFrameInfo,
        pipeline_id: PipelineId,
    ) -> Self {
        Self::new(pipeline_id, Some(parent_index), SpatialNodeType::StickyFrame(sticky_frame_info))
    }

    pub fn add_child(&mut self, child: SpatialNodeIndex) {
        self.children.push(child);
    }

    pub fn apply_old_scrolling_state(&mut self, old_scroll_info: &ScrollFrameInfo) {
        match self.node_type {
            SpatialNodeType::ScrollFrame(ref mut scrolling) => {
                *scrolling = scrolling.combine_with_old_scroll_info(old_scroll_info);
            }
            _ if old_scroll_info.offset != LayoutVector2D::zero() => {
                warn!("Tried to scroll a non-scroll node.")
            }
            _ => {}
        }
    }

    pub fn set_scroll_origin(&mut self, origin: &LayoutPoint, clamp: ScrollClamping) -> bool {
        let scrolling = match self.node_type {
            SpatialNodeType::ScrollFrame(ref mut scrolling) => scrolling,
            _ => {
                warn!("Tried to scroll a non-scroll node.");
                return false;
            }
        };

        let normalized_offset = match clamp {
            ScrollClamping::ToContentBounds => {
                let scrollable_size = scrolling.scrollable_size;
                let scrollable_width = scrollable_size.width;
                let scrollable_height = scrollable_size.height;

                if scrollable_height <= 0. && scrollable_width <= 0. {
                    return false;
                }

                let origin = LayoutPoint::new(origin.x.max(0.0), origin.y.max(0.0));
                LayoutVector2D::new(
                    (-origin.x).max(-scrollable_width).min(0.0).round(),
                    (-origin.y).max(-scrollable_height).min(0.0).round(),
                )
            }
            ScrollClamping::NoClamping => LayoutPoint::zero() - *origin,
        };

        let new_offset = normalized_offset - scrolling.external_scroll_offset;

        if new_offset == scrolling.offset {
            return false;
        }

        scrolling.offset = new_offset;
        true
    }

    pub fn mark_uninvertible(
        &mut self,
        state: &TransformUpdateState,
    ) {
        self.invertible = false;
        self.coordinate_system_id = state.current_coordinate_system_id;
        self.world_content_transform = LayoutToWorldFastTransform::identity();
        self.world_viewport_transform = LayoutToWorldFastTransform::identity();
    }

    pub fn push_gpu_data(
        &mut self,
        transform_palette: &mut TransformPalette,
        node_index: SpatialNodeIndex,
    ) {
        if self.invertible {
            transform_palette.set_world_transform(
                node_index,
                self.world_content_transform
                    .to_transform()
                    .into_owned()
            );
        }
    }

    pub fn update(
        &mut self,
        state: &mut TransformUpdateState,
        coord_systems: &mut Vec<CoordinateSystem>,
        scene_properties: &SceneProperties,
        previous_spatial_nodes: &[SpatialNode],
    ) {
        // If any of our parents was not rendered, we are not rendered either and can just
        // quit here.
        if !state.invertible {
            self.mark_uninvertible(state);
            return;
        }

        self.update_transform(state, coord_systems, scene_properties, previous_spatial_nodes);
        self.transform_kind = self.world_content_transform.kind();

        // If this node is a reference frame, we check if it has a non-invertible matrix.
        // For non-reference-frames we assume that they will produce only additional
        // translations which should be invertible.
        match self.node_type {
            SpatialNodeType::ReferenceFrame(info) if !info.invertible => {
                self.mark_uninvertible(state);
                return;
            }
            _ => self.invertible = true,
        }
    }

    pub fn update_transform(
        &mut self,
        state: &mut TransformUpdateState,
        coord_systems: &mut Vec<CoordinateSystem>,
        scene_properties: &SceneProperties,
        previous_spatial_nodes: &[SpatialNode],
    ) {
        match self.node_type {
            SpatialNodeType::ReferenceFrame(ref mut info) => {
                // Resolve the transform against any property bindings.
                let source_transform = LayoutFastTransform::from(
                    scene_properties.resolve_layout_transform(&info.source_transform)
                );

                // Do a change-basis operation on the perspective matrix using
                // the scroll offset.
                let source_transform = match info.kind {
                    ReferenceFrameKind::Perspective { scrolling_relative_to: Some(external_id) } => {
                        let scroll_offset = compute_offset_from(
                            self.parent,
                            external_id,
                            previous_spatial_nodes,
                        );

                        // Do a change-basis operation on the
                        // perspective matrix using the scroll offset.
                        source_transform
                            .pre_translate(&scroll_offset)
                            .post_translate(-scroll_offset)
                    }
                    ReferenceFrameKind::Perspective { scrolling_relative_to: None } |
                    ReferenceFrameKind::Transform => source_transform,
                };

                let resolved_transform =
                    LayoutFastTransform::with_vector(info.origin_in_parent_reference_frame)
                        .pre_mul(&source_transform);

                // The transformation for this viewport in world coordinates is the transformation for
                // our parent reference frame, plus any accumulated scrolling offsets from nodes
                // between our reference frame and this node. Finally, we also include
                // whatever local transformation this reference frame provides.
                let relative_transform = resolved_transform
                    .post_translate(state.parent_accumulated_scroll_offset)
                    .to_transform()
                    .with_destination::<LayoutPixel>();

                self.world_viewport_transform =
                    state.parent_reference_frame_transform.pre_mul(&relative_transform.into());
                self.world_content_transform = self.world_viewport_transform;

                info.invertible = self.world_viewport_transform.is_invertible();

                if info.invertible {
                    let mut reset_cs_id = match info.transform_style {
                        TransformStyle::Preserve3D => !state.preserves_3d,
                        TransformStyle::Flat => state.preserves_3d,
                    };
                    // We reset the coordinate system upon either crossing the preserve-3d context boundary,
                    // or simply a 3D transformation.
                    if !reset_cs_id {
                        // Try to update our compatible coordinate system transform. If we cannot, start a new
                        // incompatible coordinate system.
                        match ScaleOffset::from_transform(&relative_transform) {
                            Some(ref scale_offset) => {
                                self.coordinate_system_relative_scale_offset =
                                    state.coordinate_system_relative_scale_offset.accumulate(scale_offset);
                            }
                            None => reset_cs_id = true,
                        }
                    }
                    if reset_cs_id {
                        // If we break 2D axis alignment or have a perspective component, we need to start a
                        // new incompatible coordinate system with which we cannot share clips without masking.
                        self.coordinate_system_relative_scale_offset = ScaleOffset::identity();

                        let transform = state.coordinate_system_relative_scale_offset
                            .to_transform()
                            .pre_mul(&relative_transform);

                        // Push that new coordinate system and record the new id.
                        let coord_system = CoordinateSystem {
                            transform,
                            transform_style: info.transform_style,
                            parent: Some(state.current_coordinate_system_id),
                        };
                        state.current_coordinate_system_id = CoordinateSystemId(coord_systems.len() as u32);
                        coord_systems.push(coord_system);
                    }
                }

                // Ensure that the current coordinate system ID is propagated to child
                // nodes, even if we encounter a node that is not invertible. This ensures
                // that the invariant in get_relative_transform is not violated.
                self.coordinate_system_id = state.current_coordinate_system_id;
            }
            _ => {
                // We calculate this here to avoid a double-borrow later.
                let sticky_offset = self.calculate_sticky_offset(
                    &state.nearest_scrolling_ancestor_offset,
                    &state.nearest_scrolling_ancestor_viewport,
                );

                // The transformation for the bounds of our viewport is the parent reference frame
                // transform, plus any accumulated scroll offset from our parents, plus any offset
                // provided by our own sticky positioning.
                let accumulated_offset = state.parent_accumulated_scroll_offset + sticky_offset;
                self.world_viewport_transform = if accumulated_offset != LayoutVector2D::zero() {
                    state.parent_reference_frame_transform.pre_translate(&accumulated_offset)
                } else {
                    state.parent_reference_frame_transform
                };

                // The transformation for any content inside of us is the viewport transformation, plus
                // whatever scrolling offset we supply as well.
                let scroll_offset = self.scroll_offset();
                self.world_content_transform = if scroll_offset != LayoutVector2D::zero() {
                    self.world_viewport_transform.pre_translate(&scroll_offset)
                } else {
                    self.world_viewport_transform
                };

                let added_offset = state.parent_accumulated_scroll_offset + sticky_offset + scroll_offset;
                self.coordinate_system_relative_scale_offset =
                    state.coordinate_system_relative_scale_offset.offset(added_offset.to_untyped());

                if let SpatialNodeType::StickyFrame(ref mut info) = self.node_type {
                    info.current_offset = sticky_offset;
                }

                self.coordinate_system_id = state.current_coordinate_system_id;
            }
        }
    }

    fn calculate_sticky_offset(
        &self,
        viewport_scroll_offset: &LayoutVector2D,
        viewport_rect: &LayoutRect,
    ) -> LayoutVector2D {
        let info = match self.node_type {
            SpatialNodeType::StickyFrame(ref info) => info,
            _ => return LayoutVector2D::zero(),
        };

        if info.margins.top.is_none() && info.margins.bottom.is_none() &&
            info.margins.left.is_none() && info.margins.right.is_none() {
            return LayoutVector2D::zero();
        }

        // The viewport and margins of the item establishes the maximum amount that it can
        // be offset in order to keep it on screen. Since we care about the relationship
        // between the scrolled content and unscrolled viewport we adjust the viewport's
        // position by the scroll offset in order to work with their relative positions on the
        // page.
        let sticky_rect = info.frame_rect.translate(viewport_scroll_offset);

        let mut sticky_offset = LayoutVector2D::zero();
        if let Some(margin) = info.margins.top {
            let top_viewport_edge = viewport_rect.min_y() + margin;
            if sticky_rect.min_y() < top_viewport_edge {
                // If the sticky rect is positioned above the top edge of the viewport (plus margin)
                // we move it down so that it is fully inside the viewport.
                sticky_offset.y = top_viewport_edge - sticky_rect.min_y();
            } else if info.previously_applied_offset.y > 0.0 &&
                sticky_rect.min_y() > top_viewport_edge {
                // However, if the sticky rect is positioned *below* the top edge of the viewport
                // and there is already some offset applied to the sticky rect's position, then
                // we need to move it up so that it remains at the correct position. This
                // makes sticky_offset.y negative and effectively reduces the amount of the
                // offset that was already applied. We limit the reduction so that it can, at most,
                // cancel out the already-applied offset, but should never end up adjusting the
                // position the other way.
                sticky_offset.y = top_viewport_edge - sticky_rect.min_y();
                sticky_offset.y = sticky_offset.y.max(-info.previously_applied_offset.y);
            }
            debug_assert!(sticky_offset.y + info.previously_applied_offset.y >= 0.0);
        }

        // If we don't have a sticky-top offset (sticky_offset.y + info.previously_applied_offset.y
        // == 0), or if we have a previously-applied bottom offset (previously_applied_offset.y < 0)
        // then we check for handling the bottom margin case.
        if sticky_offset.y + info.previously_applied_offset.y <= 0.0 {
            if let Some(margin) = info.margins.bottom {
                // Same as the above case, but inverted for bottom-sticky items. Here
                // we adjust items upwards, resulting in a negative sticky_offset.y,
                // or reduce the already-present upward adjustment, resulting in a positive
                // sticky_offset.y.
                let bottom_viewport_edge = viewport_rect.max_y() - margin;
                if sticky_rect.max_y() > bottom_viewport_edge {
                    sticky_offset.y = bottom_viewport_edge - sticky_rect.max_y();
                } else if info.previously_applied_offset.y < 0.0 &&
                    sticky_rect.max_y() < bottom_viewport_edge {
                    sticky_offset.y = bottom_viewport_edge - sticky_rect.max_y();
                    sticky_offset.y = sticky_offset.y.min(-info.previously_applied_offset.y);
                }
                debug_assert!(sticky_offset.y + info.previously_applied_offset.y <= 0.0);
            }
        }

        // Same as above, but for the x-axis.
        if let Some(margin) = info.margins.left {
            let left_viewport_edge = viewport_rect.min_x() + margin;
            if sticky_rect.min_x() < left_viewport_edge {
                sticky_offset.x = left_viewport_edge - sticky_rect.min_x();
            } else if info.previously_applied_offset.x > 0.0 &&
                sticky_rect.min_x() > left_viewport_edge {
                sticky_offset.x = left_viewport_edge - sticky_rect.min_x();
                sticky_offset.x = sticky_offset.x.max(-info.previously_applied_offset.x);
            }
            debug_assert!(sticky_offset.x + info.previously_applied_offset.x >= 0.0);
        }

        if sticky_offset.x + info.previously_applied_offset.x <= 0.0 {
            if let Some(margin) = info.margins.right {
                let right_viewport_edge = viewport_rect.max_x() - margin;
                if sticky_rect.max_x() > right_viewport_edge {
                    sticky_offset.x = right_viewport_edge - sticky_rect.max_x();
                } else if info.previously_applied_offset.x < 0.0 &&
                    sticky_rect.max_x() < right_viewport_edge {
                    sticky_offset.x = right_viewport_edge - sticky_rect.max_x();
                    sticky_offset.x = sticky_offset.x.min(-info.previously_applied_offset.x);
                }
                debug_assert!(sticky_offset.x + info.previously_applied_offset.x <= 0.0);
            }
        }

        // The total "sticky offset" (which is the sum that was already applied by
        // the calling code, stored in info.previously_applied_offset, and the extra amount we
        // computed as a result of scrolling, stored in sticky_offset) needs to be
        // clamped to the provided bounds.
        let clamp_adjusted = |value: f32, adjust: f32, bounds: &StickyOffsetBounds| {
            (value + adjust).max(bounds.min).min(bounds.max) - adjust
        };
        sticky_offset.y = clamp_adjusted(sticky_offset.y,
                                         info.previously_applied_offset.y,
                                         &info.vertical_offset_bounds);
        sticky_offset.x = clamp_adjusted(sticky_offset.x,
                                         info.previously_applied_offset.x,
                                         &info.horizontal_offset_bounds);

        sticky_offset
    }

    pub fn prepare_state_for_children(&self, state: &mut TransformUpdateState) {
        if !self.invertible {
            state.invertible = false;
            return;
        }

        // The transformation we are passing is the transformation of the parent
        // reference frame and the offset is the accumulated offset of all the nodes
        // between us and the parent reference frame. If we are a reference frame,
        // we need to reset both these values.
        match self.node_type {
            SpatialNodeType::StickyFrame(ref info) => {
                // We don't translate the combined rect by the sticky offset, because sticky
                // offsets actually adjust the node position itself, whereas scroll offsets
                // only apply to contents inside the node.
                state.parent_accumulated_scroll_offset += info.current_offset;
                // We want nested sticky items to take into account the shift
                // we applied as well.
                state.nearest_scrolling_ancestor_offset += info.current_offset;
                state.preserves_3d = false;
            }
            SpatialNodeType::ScrollFrame(ref scrolling) => {
                state.parent_accumulated_scroll_offset += scrolling.offset;
                state.nearest_scrolling_ancestor_offset = scrolling.offset;
                state.nearest_scrolling_ancestor_viewport = scrolling.viewport_rect;
                state.preserves_3d = false;
            }
            SpatialNodeType::ReferenceFrame(ref info) => {
                state.parent_reference_frame_transform = self.world_viewport_transform;

                let should_flatten =
                    info.kind == ReferenceFrameKind::Transform &&
                    info.transform_style == TransformStyle::Flat;

                if should_flatten {
                    state.parent_reference_frame_transform = state.parent_reference_frame_transform.project_to_2d();
                }
                state.preserves_3d = info.transform_style == TransformStyle::Preserve3D;

                state.parent_accumulated_scroll_offset = LayoutVector2D::zero();
                state.coordinate_system_relative_scale_offset = self.coordinate_system_relative_scale_offset;
                let translation = -info.origin_in_parent_reference_frame;
                state.nearest_scrolling_ancestor_viewport =
                    state.nearest_scrolling_ancestor_viewport
                       .translate(&translation);
            }
        }
    }

    pub fn scroll(&mut self, scroll_location: ScrollLocation) -> bool {
        // TODO(gw): This scroll method doesn't currently support
        //           scroll nodes with non-zero external scroll
        //           offsets. However, it's never used by Gecko,
        //           which is the only client that requires
        //           non-zero external scroll offsets.

        let scrolling = match self.node_type {
            SpatialNodeType::ScrollFrame(ref mut scrolling) => scrolling,
            _ => return false,
        };

        let delta = match scroll_location {
            ScrollLocation::Delta(delta) => delta,
            ScrollLocation::Start => {
                if scrolling.offset.y.round() >= 0.0 {
                    // Nothing to do on this layer.
                    return false;
                }

                scrolling.offset.y = 0.0;
                return true;
            }
            ScrollLocation::End => {
                let end_pos = -scrolling.scrollable_size.height;
                if scrolling.offset.y.round() <= end_pos {
                    // Nothing to do on this layer.
                    return false;
                }

                scrolling.offset.y = end_pos;
                return true;
            }
        };

        let scrollable_width = scrolling.scrollable_size.width;
        let scrollable_height = scrolling.scrollable_size.height;
        let original_layer_scroll_offset = scrolling.offset;

        if scrollable_width > 0. {
            scrolling.offset.x = (scrolling.offset.x + delta.x)
                .min(0.0)
                .max(-scrollable_width)
                .round();
        }

        if scrollable_height > 0. {
            scrolling.offset.y = (scrolling.offset.y + delta.y)
                .min(0.0)
                .max(-scrollable_height)
                .round();
        }

        scrolling.offset != original_layer_scroll_offset
    }

    pub fn scroll_offset(&self) -> LayoutVector2D {
        match self.node_type {
            SpatialNodeType::ScrollFrame(ref scrolling) => scrolling.offset,
            _ => LayoutVector2D::zero(),
        }
    }

    pub fn matches_external_id(&self, external_id: ExternalScrollId) -> bool {
        match self.node_type {
            SpatialNodeType::ScrollFrame(info) if info.external_id == Some(external_id) => true,
            _ => false,
        }
    }

    pub fn transform_style(&self) -> TransformStyle {
        match self.node_type {
            SpatialNodeType::ReferenceFrame(ref info) => info.transform_style,
            SpatialNodeType::StickyFrame(_) |
            SpatialNodeType::ScrollFrame(_) => TransformStyle::Flat,
        }
    }
}

/// Defines whether we have an implicit scroll frame for a pipeline root,
/// or an explicitly defined scroll frame from the display list.
#[derive(Copy, Clone, Debug)]
pub enum ScrollFrameKind {
    PipelineRoot,
    Explicit,
}

#[derive(Copy, Clone, Debug)]
pub struct ScrollFrameInfo {
    /// The rectangle of the viewport of this scroll frame. This is important for
    /// positioning of items inside child StickyFrames.
    pub viewport_rect: LayoutRect,

    pub scroll_sensitivity: ScrollSensitivity,

    /// Amount that this ScrollFrame can scroll in both directions.
    pub scrollable_size: LayoutSize,

    /// An external id to identify this scroll frame to API clients. This
    /// allows setting scroll positions via the API without relying on ClipsIds
    /// which may change between frames.
    pub external_id: Option<ExternalScrollId>,

    /// Stores whether this is a scroll frame added implicitly by WR when adding
    /// a pipeline (either the root or an iframe). We need to exclude these
    /// when searching for scroll roots we care about for picture caching.
    /// TODO(gw): I think we can actually completely remove the implicit
    ///           scroll frame being added by WR, and rely on the embedder
    ///           to define scroll frames. However, that involves API changes
    ///           so we will use this as a temporary hack!
    pub frame_kind: ScrollFrameKind,

    /// Amount that visual components attached to this scroll node have been
    /// pre-scrolled in their local coordinates.
    pub external_scroll_offset: LayoutVector2D,

    /// The current offset of this scroll node.
    pub offset: LayoutVector2D,
}

/// Manages scrolling offset.
impl ScrollFrameInfo {
    pub fn new(
        viewport_rect: LayoutRect,
        scroll_sensitivity: ScrollSensitivity,
        scrollable_size: LayoutSize,
        external_id: Option<ExternalScrollId>,
        frame_kind: ScrollFrameKind,
        external_scroll_offset: LayoutVector2D,
    ) -> ScrollFrameInfo {
        ScrollFrameInfo {
            viewport_rect,
            offset: -external_scroll_offset,
            scroll_sensitivity,
            scrollable_size,
            external_id,
            frame_kind,
            external_scroll_offset,
        }
    }

    pub fn sensitive_to_input_events(&self) -> bool {
        match self.scroll_sensitivity {
            ScrollSensitivity::ScriptAndInputEvents => true,
            ScrollSensitivity::Script => false,
        }
    }

    pub fn combine_with_old_scroll_info(
        self,
        old_scroll_info: &ScrollFrameInfo
    ) -> ScrollFrameInfo {
        let offset =
            old_scroll_info.offset +
            self.external_scroll_offset -
            old_scroll_info.external_scroll_offset;

        ScrollFrameInfo {
            viewport_rect: self.viewport_rect,
            offset,
            scroll_sensitivity: self.scroll_sensitivity,
            scrollable_size: self.scrollable_size,
            external_id: self.external_id,
            frame_kind: self.frame_kind,
            external_scroll_offset: self.external_scroll_offset,
        }
    }
}

/// Contains information about reference frames.
#[derive(Copy, Clone, Debug)]
pub struct ReferenceFrameInfo {
    /// The source transform and perspective matrices provided by the stacking context
    /// that forms this reference frame. We maintain the property binding information
    /// here so that we can resolve the animated transform and update the tree each
    /// frame.
    pub source_transform: PropertyBinding<LayoutTransform>,
    pub transform_style: TransformStyle,
    pub kind: ReferenceFrameKind,

    /// The original, not including the transform and relative to the parent reference frame,
    /// origin of this reference frame. This is already rolled into the `transform' property, but
    /// we also store it here to properly transform the viewport for sticky positioning.
    pub origin_in_parent_reference_frame: LayoutVector2D,

    /// True if the resolved transform is invertible.
    pub invertible: bool,
}

#[derive(Clone, Debug)]
pub struct StickyFrameInfo {
    pub frame_rect: LayoutRect,
    pub margins: SideOffsets2D<Option<f32>>,
    pub vertical_offset_bounds: StickyOffsetBounds,
    pub horizontal_offset_bounds: StickyOffsetBounds,
    pub previously_applied_offset: LayoutVector2D,
    pub current_offset: LayoutVector2D,
}

impl StickyFrameInfo {
    pub fn new(
        frame_rect: LayoutRect,
        margins: SideOffsets2D<Option<f32>>,
        vertical_offset_bounds: StickyOffsetBounds,
        horizontal_offset_bounds: StickyOffsetBounds,
        previously_applied_offset: LayoutVector2D
    ) -> StickyFrameInfo {
        StickyFrameInfo {
            frame_rect,
            margins,
            vertical_offset_bounds,
            horizontal_offset_bounds,
            previously_applied_offset,
            current_offset: LayoutVector2D::zero(),
        }
    }
}

#[test]
fn test_cst_perspective_relative_scroll() {
    // Verify that when computing the offset from a perspective transform
    // to a relative scroll node that any external scroll offset is
    // ignored. This is because external scroll offsets are not
    // propagated across reference frame boundaries.

    // It's not currently possible to verify this with a wrench reftest,
    // since wrench doesn't understand external scroll ids. When wrench
    // supports this, we could also verify with a reftest.

    use clip_scroll_tree::ClipScrollTree;
    use euclid::approxeq::ApproxEq;

    let mut cst = ClipScrollTree::new();
    let pipeline_id = PipelineId::dummy();
    let ext_scroll_id = ExternalScrollId(1, pipeline_id);
    let transform = LayoutTransform::create_perspective(100.0);

    let root = cst.add_reference_frame(
        None,
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform,
        LayoutVector2D::zero(),
        pipeline_id,
    );

    let scroll_frame_1 = cst.add_scroll_frame(
        root,
        Some(ext_scroll_id),
        pipeline_id,
        &LayoutRect::new(LayoutPoint::zero(), LayoutSize::new(100.0, 100.0)),
        &LayoutSize::new(100.0, 500.0),
        ScrollSensitivity::Script,
        ScrollFrameKind::Explicit,
        LayoutVector2D::zero(),
    );

    let scroll_frame_2 = cst.add_scroll_frame(
        scroll_frame_1,
        None,
        pipeline_id,
        &LayoutRect::new(LayoutPoint::zero(), LayoutSize::new(100.0, 100.0)),
        &LayoutSize::new(100.0, 500.0),
        ScrollSensitivity::Script,
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 50.0),
    );

    let ref_frame = cst.add_reference_frame(
        Some(scroll_frame_2),
        TransformStyle::Preserve3D,
        PropertyBinding::Value(transform),
        ReferenceFrameKind::Perspective {
            scrolling_relative_to: Some(ext_scroll_id),
        },
        LayoutVector2D::zero(),
        pipeline_id,
    );

    cst.update_tree(WorldPoint::zero(), &SceneProperties::new(), None);

    let scroll_offset = compute_offset_from(
        cst.spatial_nodes[ref_frame.0 as usize].parent,
        ext_scroll_id,
        &cst.spatial_nodes,
    );

    assert!(scroll_offset.x.approx_eq(&0.0));
    assert!(scroll_offset.y.approx_eq(&0.0));
}

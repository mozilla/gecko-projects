/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use bincode;
use euclid::SideOffsets2D;
#[cfg(feature = "deserialize")]
use serde::de::Deserializer;
#[cfg(feature = "serialize")]
use serde::ser::{Serializer, SerializeSeq};
use serde::{Deserialize, Serialize};
use std::io::{Read, stdout, Write};
use std::marker::PhantomData;
use std::ops::Range;
use std::{io, mem, ptr, slice};
use time::precise_time_ns;
// local imports
use display_item as di;
use api::{PipelineId, PropertyBinding};
use gradient_builder::GradientBuilder;
use color::ColorF;
use font::{FontInstanceKey, GlyphInstance, GlyphOptions};
use image::{ColorDepth, ImageKey};
use units::*;


// We don't want to push a long text-run. If a text-run is too long, split it into several parts.
// This needs to be set to (renderer::MAX_VERTEX_TEXTURE_WIDTH - VECS_PER_TEXT_RUN) * 2
pub const MAX_TEXT_RUN_LENGTH: usize = 2040;

// See ROOT_REFERENCE_FRAME_SPATIAL_ID and ROOT_SCROLL_NODE_SPATIAL_ID
// TODO(mrobinson): It would be a good idea to eliminate the root scroll frame which is only
// used by Servo.
const FIRST_SPATIAL_NODE_INDEX: usize = 2;

// See ROOT_SCROLL_NODE_SPATIAL_ID
const FIRST_CLIP_NODE_INDEX: usize = 1;

#[repr(C)]
#[derive(Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub struct ItemRange<T> {
    start: usize,
    length: usize,
    _boo: PhantomData<T>,
}

impl<T> Copy for ItemRange<T> {}
impl<T> Clone for ItemRange<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T> Default for ItemRange<T> {
    fn default() -> Self {
        ItemRange {
            start: 0,
            length: 0,
            _boo: PhantomData,
        }
    }
}

impl<T> ItemRange<T> {
    pub fn is_empty(&self) -> bool {
        // Nothing more than space for a length (0).
        self.length <= mem::size_of::<u64>()
    }
}

pub struct TempFilterData {
    pub func_types: ItemRange<di::ComponentTransferFuncType>,
    pub r_values: ItemRange<f32>,
    pub g_values: ItemRange<f32>,
    pub b_values: ItemRange<f32>,
    pub a_values: ItemRange<f32>,
}

/// A display list.
#[derive(Clone, Default)]
pub struct BuiltDisplayList {
    /// Serde encoded bytes. Mostly DisplayItems, but some mixed in slices.
    data: Vec<u8>,
    descriptor: BuiltDisplayListDescriptor,
}

/// Describes the memory layout of a display list.
///
/// A display list consists of some number of display list items, followed by a number of display
/// items.
#[repr(C)]
#[derive(Copy, Clone, Default, Deserialize, Serialize)]
pub struct BuiltDisplayListDescriptor {
    /// The first IPC time stamp: before any work has been done
    builder_start_time: u64,
    /// The second IPC time stamp: after serialization
    builder_finish_time: u64,
    /// The third IPC time stamp: just before sending
    send_start_time: u64,
    /// The amount of clipping nodes created while building this display list.
    total_clip_nodes: usize,
    /// The amount of spatial nodes created while building this display list.
    total_spatial_nodes: usize,
}

pub struct BuiltDisplayListIter<'a> {
    list: &'a BuiltDisplayList,
    data: &'a [u8],
    cur_item: di::DisplayItem,
    cur_stops: ItemRange<di::GradientStop>,
    cur_glyphs: ItemRange<GlyphInstance>,
    cur_filters: ItemRange<di::FilterOp>,
    cur_filter_data: Vec<TempFilterData>,
    cur_clip_chain_items: ItemRange<di::ClipId>,
    cur_complex_clip: (ItemRange<di::ComplexClipRegion>, usize),
    peeking: Peek,
}

pub struct DisplayItemRef<'a: 'b, 'b> {
    iter: &'b BuiltDisplayListIter<'a>,
}

#[derive(PartialEq)]
enum Peek {
    StartPeeking,
    IsPeeking,
    NotPeeking,
}

#[derive(Clone)]
pub struct AuxIter<'a, T> {
    data: &'a [u8],
    size: usize,
    _boo: PhantomData<T>,
}

impl BuiltDisplayListDescriptor {}

impl BuiltDisplayList {
    pub fn from_data(data: Vec<u8>, descriptor: BuiltDisplayListDescriptor) -> Self {
        BuiltDisplayList { data, descriptor }
    }

    pub fn into_data(mut self) -> (Vec<u8>, BuiltDisplayListDescriptor) {
        self.descriptor.send_start_time = precise_time_ns();
        (self.data, self.descriptor)
    }

    pub fn data(&self) -> &[u8] {
        &self.data[..]
    }

    // Currently redundant with data, but may be useful if we add extra data to dl
    pub fn item_slice(&self) -> &[u8] {
        &self.data[..]
    }

    pub fn descriptor(&self) -> &BuiltDisplayListDescriptor {
        &self.descriptor
    }

    pub fn times(&self) -> (u64, u64, u64) {
        (
            self.descriptor.builder_start_time,
            self.descriptor.builder_finish_time,
            self.descriptor.send_start_time,
        )
    }

    pub fn total_clip_nodes(&self) -> usize {
        self.descriptor.total_clip_nodes
    }

    pub fn total_spatial_nodes(&self) -> usize {
        self.descriptor.total_spatial_nodes
    }

    pub fn iter(&self) -> BuiltDisplayListIter {
        BuiltDisplayListIter::new(self)
    }

    pub fn get<'de, T: Deserialize<'de>>(&self, range: ItemRange<T>) -> AuxIter<T> {
        AuxIter::new(&self.data[range.start .. range.start + range.length])
    }
}

/// Returns the byte-range the slice occupied, and the number of elements
/// in the slice.
fn skip_slice<T: for<'de> Deserialize<'de>>(
    list: &BuiltDisplayList,
    mut data: &mut &[u8],
) -> (ItemRange<T>, usize) {
    let base = list.data.as_ptr() as usize;

    let byte_size: usize = bincode::deserialize_from(&mut data)
                                    .expect("MEH: malicious input?");
    let start = data.as_ptr() as usize;
    let item_count: usize = bincode::deserialize_from(&mut data)
                                    .expect("MEH: malicious input?");

    // Remember how many bytes item_count occupied
    let item_count_size = data.as_ptr() as usize - start;

    let range = ItemRange {
        start: start - base,                      // byte offset to item_count
        length: byte_size + item_count_size,      // number of bytes for item_count + payload
        _boo: PhantomData,
    };

    // Adjust data pointer to skip read values
    *data = &data[byte_size ..];
    (range, item_count)
}


impl<'a> BuiltDisplayListIter<'a> {
    pub fn new(list: &'a BuiltDisplayList) -> Self {
        Self::new_with_list_and_data(list, list.item_slice())
    }

    pub fn new_with_list_and_data(list: &'a BuiltDisplayList, data: &'a [u8]) -> Self {
        BuiltDisplayListIter {
            list,
            data,
            cur_item: di::DisplayItem {
                // Dummy data, will be overwritten by `next`
                item: di::SpecificDisplayItem::PopStackingContext,
                layout: di::LayoutPrimitiveInfo::new(LayoutRect::zero()),
                space_and_clip: di::SpaceAndClipInfo::root_scroll(PipelineId::dummy())
            },
            cur_stops: ItemRange::default(),
            cur_glyphs: ItemRange::default(),
            cur_filters: ItemRange::default(),
            cur_filter_data: Vec::new(),
            cur_clip_chain_items: ItemRange::default(),
            cur_complex_clip: (ItemRange::default(), 0),
            peeking: Peek::NotPeeking,
        }
    }

    pub fn display_list(&self) -> &'a BuiltDisplayList {
        self.list
    }

    pub fn next<'b>(&'b mut self) -> Option<DisplayItemRef<'a, 'b>> {
        use SpecificDisplayItem::*;

        match self.peeking {
            Peek::IsPeeking => {
                self.peeking = Peek::NotPeeking;
                return Some(self.as_ref());
            }
            Peek::StartPeeking => {
                self.peeking = Peek::IsPeeking;
            }
            Peek::NotPeeking => { /* do nothing */ }
        }

        // Don't let these bleed into another item
        self.cur_stops = ItemRange::default();
        self.cur_complex_clip = (ItemRange::default(), 0);
        self.cur_clip_chain_items = ItemRange::default();

        loop {
            self.next_raw()?;
            if let SetGradientStops = self.cur_item.item {
                // SetGradientStops is a dummy item that most consumers should ignore
                continue;
            }
            if let SetFilterOps = self.cur_item.item {
                // SetFilterOps is a dummy item that most consumers should ignore
                continue;
            }
            if let SetFilterData = self.cur_item.item {
                // SetFilterData is a dummy item that most consumers should ignore
                continue;
            }

            break;
        }

        Some(self.as_ref())
    }

    /// Gets the next display item, even if it's a dummy. Also doesn't handle peeking
    /// and may leave irrelevant ranges live (so a Clip may have GradientStops if
    /// for some reason you ask).
    pub fn next_raw<'b>(&'b mut self) -> Option<DisplayItemRef<'a, 'b>> {
        use SpecificDisplayItem::*;

        if self.data.is_empty() {
            return None;
        }

        {
            let reader = bincode::IoReader::new(UnsafeReader::new(&mut self.data));
            bincode::deserialize_in_place(reader, &mut self.cur_item)
                .expect("MEH: malicious process?");
        }

        match self.cur_item.item {
            SetGradientStops => {
                self.cur_stops = skip_slice::<di::GradientStop>(self.list, &mut self.data).0;
            }
            SetFilterOps => {
                self.cur_filters = skip_slice::<di::FilterOp>(self.list, &mut self.data).0;
            }
            SetFilterData => {
                self.cur_filter_data.push(TempFilterData {
                    func_types: skip_slice::<di::ComponentTransferFuncType>(self.list, &mut self.data).0,
                    r_values: skip_slice::<f32>(self.list, &mut self.data).0,
                    g_values: skip_slice::<f32>(self.list, &mut self.data).0,
                    b_values: skip_slice::<f32>(self.list, &mut self.data).0,
                    a_values: skip_slice::<f32>(self.list, &mut self.data).0,
                });
            }
            ClipChain(_) => {
                self.cur_clip_chain_items = skip_slice::<di::ClipId>(self.list, &mut self.data).0;
            }
            Clip(_) | ScrollFrame(_) => {
                self.cur_complex_clip = self.skip_slice::<di::ComplexClipRegion>()
            }
            Text(_) => self.cur_glyphs = self.skip_slice::<GlyphInstance>().0,
            _ => { /* do nothing */ }
        }

        Some(self.as_ref())
    }

    fn skip_slice<T: for<'de> Deserialize<'de>>(&mut self) -> (ItemRange<T>, usize) {
        skip_slice::<T>(self.list, &mut self.data)
    }

    pub fn as_ref<'b>(&'b self) -> DisplayItemRef<'a, 'b> {
        DisplayItemRef { iter: self }
    }

    pub fn starting_stacking_context(
        &mut self,
    ) -> Option<(di::StackingContext, LayoutRect, ItemRange<di::FilterOp>)> {
        self.next().and_then(|item| match *item.item() {
            di::SpecificDisplayItem::PushStackingContext(ref specific_item) => Some((
                specific_item.stacking_context,
                item.rect(),
                item.filters(),
            )),
            _ => None,
        })
    }

    pub fn skip_current_stacking_context(&mut self) {
        let mut depth = 0;
        while let Some(item) = self.next() {
            match *item.item() {
                di::SpecificDisplayItem::PushStackingContext(..) => depth += 1,
                di::SpecificDisplayItem::PopStackingContext if depth == 0 => return,
                di::SpecificDisplayItem::PopStackingContext => depth -= 1,
                _ => {}
            }
            debug_assert!(depth >= 0);
        }
    }

    pub fn current_stacking_context_empty(&mut self) -> bool {
        match self.peek() {
            Some(item) => *item.item() == di::SpecificDisplayItem::PopStackingContext,
            None => true,
        }
    }

    pub fn peek<'b>(&'b mut self) -> Option<DisplayItemRef<'a, 'b>> {
        if self.peeking == Peek::NotPeeking {
            self.peeking = Peek::StartPeeking;
            self.next()
        } else {
            Some(self.as_ref())
        }
    }
}

// Some of these might just become ItemRanges
impl<'a, 'b> DisplayItemRef<'a, 'b> {
    pub fn display_item(&self) -> &di::DisplayItem {
        &self.iter.cur_item
    }

    pub fn rect(&self) -> LayoutRect {
        self.iter.cur_item.layout.rect
    }

    pub fn get_layout_primitive_info(&self, offset: &LayoutVector2D) -> di::LayoutPrimitiveInfo {
        let layout = self.iter.cur_item.layout;
        di::LayoutPrimitiveInfo {
            rect: layout.rect.translate(offset),
            clip_rect: layout.clip_rect.translate(offset),
            is_backface_visible: layout.is_backface_visible,
            tag: layout.tag,
        }
    }

    pub fn clip_rect(&self) -> &LayoutRect {
        &self.iter.cur_item.layout.clip_rect
    }

    pub fn space_and_clip_info(&self) -> &di::SpaceAndClipInfo {
        &self.iter.cur_item.space_and_clip
    }

    pub fn item(&self) -> &di::SpecificDisplayItem {
        &self.iter.cur_item.item
    }

    pub fn complex_clip(&self) -> (ItemRange<di::ComplexClipRegion>, usize) {
        self.iter.cur_complex_clip
    }

    pub fn gradient_stops(&self) -> ItemRange<di::GradientStop> {
        self.iter.cur_stops
    }

    pub fn glyphs(&self) -> ItemRange<GlyphInstance> {
        self.iter.cur_glyphs
    }

    pub fn filters(&self) -> ItemRange<di::FilterOp> {
        self.iter.cur_filters
    }

    pub fn filter_datas(&self) -> &Vec<TempFilterData> {
        &self.iter.cur_filter_data
    }

    pub fn clip_chain_items(&self) -> ItemRange<di::ClipId> {
        self.iter.cur_clip_chain_items
    }

    pub fn display_list(&self) -> &BuiltDisplayList {
        self.iter.display_list()
    }

    pub fn is_backface_visible(&self) -> bool {
        self.iter.cur_item.layout.is_backface_visible
    }

    // Creates a new iterator where this element's iterator is, to hack around borrowck.
    pub fn sub_iter(&self) -> BuiltDisplayListIter<'a> {
        BuiltDisplayListIter::new_with_list_and_data(self.iter.list, self.iter.data)
    }
}

impl<'de, 'a, T: Deserialize<'de>> AuxIter<'a, T> {
    pub fn new(mut data: &'a [u8]) -> Self {
        let size: usize = if data.is_empty() {
            0 // Accept empty ItemRanges pointing anywhere
        } else {
            bincode::deserialize_from(&mut UnsafeReader::new(&mut data)).expect("MEH: malicious input?")
        };

        AuxIter {
            data,
            size,
            _boo: PhantomData,
        }
    }
}

impl<'a, T: for<'de> Deserialize<'de>> Iterator for AuxIter<'a, T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        if self.size == 0 {
            None
        } else {
            self.size -= 1;
            Some(
                bincode::deserialize_from(&mut UnsafeReader::new(&mut self.data))
                    .expect("MEH: malicious input?"),
            )
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.size, Some(self.size))
    }
}

impl<'a, T: for<'de> Deserialize<'de>> ::std::iter::ExactSizeIterator for AuxIter<'a, T> {}


#[cfg(feature = "serialize")]
impl Serialize for BuiltDisplayList {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        use display_item::CompletelySpecificDisplayItem::*;
        use display_item::GenericDisplayItem;

        let mut seq = serializer.serialize_seq(None)?;
        let mut traversal = self.iter();
        while let Some(item) = traversal.next_raw() {
            let display_item = item.display_item();
            let serial_di = GenericDisplayItem {
                item: match display_item.item {
                    di::SpecificDisplayItem::Clip(v) => Clip(
                        v,
                        item.iter.list.get(item.iter.cur_complex_clip.0).collect()
                    ),
                    di::SpecificDisplayItem::ClipChain(v) => ClipChain(
                        v,
                        item.iter.list.get(item.iter.cur_clip_chain_items).collect(),
                    ),
                    di::SpecificDisplayItem::ScrollFrame(v) => ScrollFrame(
                        v,
                        item.iter.list.get(item.iter.cur_complex_clip.0).collect()
                    ),
                    di::SpecificDisplayItem::StickyFrame(v) => StickyFrame(v),
                    di::SpecificDisplayItem::Rectangle(v) => Rectangle(v),
                    di::SpecificDisplayItem::ClearRectangle => ClearRectangle,
                    di::SpecificDisplayItem::Line(v) => Line(v),
                    di::SpecificDisplayItem::Text(v) => Text(
                        v,
                        item.iter.list.get(item.iter.cur_glyphs).collect()
                    ),
                    di::SpecificDisplayItem::Image(v) => Image(v),
                    di::SpecificDisplayItem::YuvImage(v) => YuvImage(v),
                    di::SpecificDisplayItem::Border(v) => Border(v),
                    di::SpecificDisplayItem::BoxShadow(v) => BoxShadow(v),
                    di::SpecificDisplayItem::Gradient(v) => Gradient(v),
                    di::SpecificDisplayItem::RadialGradient(v) => RadialGradient(v),
                    di::SpecificDisplayItem::Iframe(v) => Iframe(v),
                    di::SpecificDisplayItem::PushReferenceFrame(v) => PushReferenceFrame(v),
                    di::SpecificDisplayItem::PopReferenceFrame => PopReferenceFrame,
                    di::SpecificDisplayItem::PushStackingContext(v) => PushStackingContext(v),
                    di::SpecificDisplayItem::PopStackingContext => PopStackingContext,
                    di::SpecificDisplayItem::SetFilterOps => SetFilterOps(
                        item.iter.list.get(item.iter.cur_filters).collect()
                    ),
                    di::SpecificDisplayItem::SetFilterData => {
                        debug_assert!(!item.iter.cur_filter_data.is_empty());
                        let temp_filter_data = &item.iter.cur_filter_data[item.iter.cur_filter_data.len()-1];

                        let func_types: Vec<di::ComponentTransferFuncType> =
                            item.iter.list.get(temp_filter_data.func_types).collect();
                        debug_assert!(func_types.len() == 4);
                        SetFilterData(di::FilterData {
                            func_r_type: func_types[0],
                            r_values: item.iter.list.get(temp_filter_data.r_values).collect(),
                            func_g_type: func_types[1],
                            g_values: item.iter.list.get(temp_filter_data.g_values).collect(),
                            func_b_type: func_types[2],
                            b_values: item.iter.list.get(temp_filter_data.b_values).collect(),
                            func_a_type: func_types[3],
                            a_values: item.iter.list.get(temp_filter_data.a_values).collect(),
                        })
                    },
                    di::SpecificDisplayItem::SetGradientStops => SetGradientStops(
                        item.iter.list.get(item.iter.cur_stops).collect()
                    ),
                    di::SpecificDisplayItem::PushShadow(v) => PushShadow(v),
                    di::SpecificDisplayItem::PopAllShadows => PopAllShadows,
                    di::SpecificDisplayItem::PushCacheMarker(m) => PushCacheMarker(m),
                    di::SpecificDisplayItem::PopCacheMarker => PopCacheMarker,
                },
                layout: display_item.layout,
                space_and_clip: display_item.space_and_clip,
            };
            seq.serialize_element(&serial_di)?
        }
        seq.end()
    }
}

// The purpose of this implementation is to deserialize
// a display list from one format just to immediately
// serialize then into a "built" `Vec<u8>`.

#[cfg(feature = "deserialize")]
impl<'de> Deserialize<'de> for BuiltDisplayList {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        use display_item::CompletelySpecificDisplayItem::*;
        use display_item::{CompletelySpecificDisplayItem, GenericDisplayItem};

        let list = Vec::<GenericDisplayItem<CompletelySpecificDisplayItem>>
            ::deserialize(deserializer)?;

        let mut data = Vec::new();
        let mut temp = Vec::new();
        let mut total_clip_nodes = FIRST_CLIP_NODE_INDEX;
        let mut total_spatial_nodes = FIRST_SPATIAL_NODE_INDEX;
        for complete in list {
            let item = di::DisplayItem {
                item: match complete.item {
                    Clip(specific_item, complex_clips) => {
                        total_clip_nodes += 1;
                        DisplayListBuilder::push_iter_impl(&mut temp, complex_clips);
                        di::SpecificDisplayItem::Clip(specific_item)
                    },
                    ClipChain(specific_item, clip_chain_ids) => {
                        DisplayListBuilder::push_iter_impl(&mut temp, clip_chain_ids);
                        di::SpecificDisplayItem::ClipChain(specific_item)
                    }
                    ScrollFrame(specific_item, complex_clips) => {
                        total_spatial_nodes += 1;
                        total_clip_nodes += 1;
                        DisplayListBuilder::push_iter_impl(&mut temp, complex_clips);
                        di::SpecificDisplayItem::ScrollFrame(specific_item)
                    }
                    StickyFrame(specific_item) => {
                        total_spatial_nodes += 1;
                        di::SpecificDisplayItem::StickyFrame(specific_item)
                    }
                    Rectangle(specific_item) => di::SpecificDisplayItem::Rectangle(specific_item),
                    ClearRectangle => di::SpecificDisplayItem::ClearRectangle,
                    Line(specific_item) => di::SpecificDisplayItem::Line(specific_item),
                    Text(specific_item, glyphs) => {
                        DisplayListBuilder::push_iter_impl(&mut temp, glyphs);
                        di::SpecificDisplayItem::Text(specific_item)
                    },
                    Image(specific_item) => di::SpecificDisplayItem::Image(specific_item),
                    YuvImage(specific_item) => di::SpecificDisplayItem::YuvImage(specific_item),
                    Border(specific_item) => di::SpecificDisplayItem::Border(specific_item),
                    BoxShadow(specific_item) => di::SpecificDisplayItem::BoxShadow(specific_item),
                    Gradient(specific_item) => di::SpecificDisplayItem::Gradient(specific_item),
                    RadialGradient(specific_item) =>
                        di::SpecificDisplayItem::RadialGradient(specific_item),
                    Iframe(specific_item) => {
                        total_clip_nodes += 1;
                        di::SpecificDisplayItem::Iframe(specific_item)
                    }
                    PushReferenceFrame(v) => {
                        total_spatial_nodes += 1;
                        di::SpecificDisplayItem::PushReferenceFrame(v)
                    }
                    PopReferenceFrame => di::SpecificDisplayItem::PopReferenceFrame,
                    PushStackingContext(specific_item) => {
                        di::SpecificDisplayItem::PushStackingContext(specific_item)
                    },
                    SetFilterOps(filters) => {
                        DisplayListBuilder::push_iter_impl(&mut temp, filters);
                        di::SpecificDisplayItem::SetFilterOps
                    },
                    SetFilterData(filter_data) => {
                        let func_types: Vec<di::ComponentTransferFuncType> =
                            [filter_data.func_r_type,
                             filter_data.func_g_type,
                             filter_data.func_b_type,
                             filter_data.func_a_type].to_vec();
                        DisplayListBuilder::push_iter_impl(&mut temp, func_types);
                        DisplayListBuilder::push_iter_impl(&mut temp, filter_data.r_values);
                        DisplayListBuilder::push_iter_impl(&mut temp, filter_data.g_values);
                        DisplayListBuilder::push_iter_impl(&mut temp, filter_data.b_values);
                        DisplayListBuilder::push_iter_impl(&mut temp, filter_data.a_values);
                        di::SpecificDisplayItem::SetFilterData
                    },
                    PopStackingContext => di::SpecificDisplayItem::PopStackingContext,
                    SetGradientStops(stops) => {
                        DisplayListBuilder::push_iter_impl(&mut temp, stops);
                        di::SpecificDisplayItem::SetGradientStops
                    },
                    PushShadow(specific_item) => di::SpecificDisplayItem::PushShadow(specific_item),
                    PopAllShadows => di::SpecificDisplayItem::PopAllShadows,
                    PushCacheMarker(marker) => di::SpecificDisplayItem::PushCacheMarker(marker),
                    PopCacheMarker => di::SpecificDisplayItem::PopCacheMarker,
                },
                layout: complete.layout,
                space_and_clip: complete.space_and_clip,
            };
            serialize_fast(&mut data, &item);
            // the aux data is serialized after the item, hence the temporary
            data.extend(temp.drain(..));
        }

        Ok(BuiltDisplayList {
            data,
            descriptor: BuiltDisplayListDescriptor {
                builder_start_time: 0,
                builder_finish_time: 1,
                send_start_time: 0,
                total_clip_nodes,
                total_spatial_nodes,
            },
        })
    }
}

// This is a replacement for bincode::serialize_into(&vec)
// The default implementation Write for Vec will basically
// call extend_from_slice(). Serde ends up calling that for every
// field of a struct that we're serializing. extend_from_slice()
// does not get inlined and thus we end up calling a generic memcpy()
// implementation. If we instead reserve enough room for the serialized
// struct in the Vec ahead of time we can rely on that and use
// the following UnsafeVecWriter to write into the vec without
// any checks. This writer assumes that size returned by the
// serialize function will not change between calls to serialize_into:
//
// For example, the following struct will cause memory unsafety when
// used with UnsafeVecWriter.
//
// struct S {
//    first: Cell<bool>,
// }
//
// impl Serialize for S {
//    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
//        where S: Serializer
//    {
//        if self.first.get() {
//            self.first.set(false);
//            ().serialize(serializer)
//        } else {
//            0.serialize(serializer)
//        }
//    }
// }
//

struct UnsafeVecWriter(*mut u8);

impl Write for UnsafeVecWriter {
    #[inline(always)]
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        unsafe {
            ptr::copy_nonoverlapping(buf.as_ptr(), self.0, buf.len());
            self.0 = self.0.add(buf.len());
        }
        Ok(buf.len())
    }

    #[inline(always)]
    fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        unsafe {
            ptr::copy_nonoverlapping(buf.as_ptr(), self.0, buf.len());
            self.0 = self.0.add(buf.len());
        }
        Ok(())
    }

    #[inline(always)]
    fn flush(&mut self) -> io::Result<()> { Ok(()) }
}

struct SizeCounter(usize);

impl<'a> Write for SizeCounter {
    #[inline(always)]
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0 += buf.len();
        Ok(buf.len())
    }

    #[inline(always)]
    fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        self.0 += buf.len();
        Ok(())
    }

    #[inline(always)]
    fn flush(&mut self) -> io::Result<()> { Ok(()) }
}

/// Serializes a value assuming the Serialize impl has a stable size across two
/// invocations.
///
/// If this assumption is incorrect, the result will be Undefined Behaviour. This
/// assumption should hold for all derived Serialize impls, which is all we currently
/// use.
fn serialize_fast<T: Serialize>(vec: &mut Vec<u8>, e: T) {
    // manually counting the size is faster than vec.reserve(bincode::serialized_size(&e) as usize) for some reason
    let mut size = SizeCounter(0);
    bincode::serialize_into(&mut size, &e).unwrap();
    vec.reserve(size.0);

    let old_len = vec.len();
    let ptr = unsafe { vec.as_mut_ptr().add(old_len) };
    let mut w = UnsafeVecWriter(ptr);
    bincode::serialize_into(&mut w, &e).unwrap();

    // fix up the length
    unsafe { vec.set_len(old_len + size.0); }

    // make sure we wrote the right amount
    debug_assert_eq!(((w.0 as usize) - (vec.as_ptr() as usize)), vec.len());
}

/// Serializes an iterator, assuming:
///
/// * The Clone impl is trivial (e.g. we're just memcopying a slice iterator)
/// * The ExactSizeIterator impl is stable and correct across a Clone
/// * The Serialize impl has a stable size across two invocations
///
/// If the first is incorrect, WebRender will be very slow. If the other two are
/// incorrect, the result will be Undefined Behaviour! The ExactSizeIterator
/// bound would ideally be replaced with a TrustedLen bound to protect us a bit
/// better, but that trait isn't stable (and won't be for a good while, if ever).
///
/// Debug asserts are included that should catch all Undefined Behaviour, but
/// we can't afford to include these in release builds.
fn serialize_iter_fast<I>(vec: &mut Vec<u8>, iter: I) -> usize
where I: ExactSizeIterator + Clone,
      I::Item: Serialize,
{
    // manually counting the size is faster than vec.reserve(bincode::serialized_size(&e) as usize) for some reason
    let mut size = SizeCounter(0);
    let mut count1 = 0;

    for e in iter.clone() {
        bincode::serialize_into(&mut size, &e).unwrap();
        count1 += 1;
    }

    vec.reserve(size.0);

    let old_len = vec.len();
    let ptr = unsafe { vec.as_mut_ptr().add(old_len) };
    let mut w = UnsafeVecWriter(ptr);
    let mut count2 = 0;

    for e in iter {
        bincode::serialize_into(&mut w, &e).unwrap();
        count2 += 1;
    }

    // fix up the length
    unsafe { vec.set_len(old_len + size.0); }

    // make sure we wrote the right amount
    debug_assert_eq!(((w.0 as usize) - (vec.as_ptr() as usize)), vec.len());
    debug_assert_eq!(count1, count2);

    count1
}

// This uses a (start, end) representation instead of (start, len) so that
// only need to update a single field as we read through it. This
// makes it easier for llvm to understand what's going on. (https://github.com/rust-lang/rust/issues/45068)
// We update the slice only once we're done reading
struct UnsafeReader<'a: 'b, 'b> {
    start: *const u8,
    end: *const u8,
    slice: &'b mut &'a [u8],
}

impl<'a, 'b> UnsafeReader<'a, 'b> {
    #[inline(always)]
    fn new(buf: &'b mut &'a [u8]) -> UnsafeReader<'a, 'b> {
        unsafe {
            let end = buf.as_ptr().add(buf.len());
            let start = buf.as_ptr();
            UnsafeReader { start, end, slice: buf }
        }
    }

    // This read implementation is significantly faster than the standard &[u8] one.
    //
    // First, it only supports reading exactly buf.len() bytes. This ensures that
    // the argument to memcpy is always buf.len() and will allow a constant buf.len()
    // to be propagated through to memcpy which LLVM will turn into explicit loads and
    // stores. The standard implementation does a len = min(slice.len(), buf.len())
    //
    // Second, we only need to adjust 'start' after reading and it's only adjusted by a
    // constant. This allows LLVM to avoid adjusting the length field after ever read
    // and lets it be aggregated into a single adjustment.
    #[inline(always)]
    fn read_internal(&mut self, buf: &mut [u8]) {
        // this is safe because we panic if start + buf.len() > end
        unsafe {
            assert!(self.start.add(buf.len()) <= self.end, "UnsafeReader: read past end of target");
            ptr::copy_nonoverlapping(self.start, buf.as_mut_ptr(), buf.len());
            self.start = self.start.add(buf.len());
        }
    }
}

impl<'a, 'b> Drop for UnsafeReader<'a, 'b> {
    // this adjusts input slice so that it properly represents the amount that's left.
    #[inline(always)]
    fn drop(&mut self) {
        // this is safe because we know that start and end are contained inside the original slice
        unsafe {
            *self.slice = slice::from_raw_parts(self.start, (self.end as usize) - (self.start as usize));
        }
    }
}

impl<'a, 'b> Read for UnsafeReader<'a, 'b> {
    // These methods were not being inlined and we need them to be so that the memcpy
    // is for a constant size
    #[inline(always)]
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.read_internal(buf);
        Ok(buf.len())
    }
    #[inline(always)]
    fn read_exact(&mut self, buf: &mut [u8]) -> io::Result<()> {
        self.read_internal(buf);
        Ok(())
    }
}

#[derive(Clone, Debug)]
pub struct SaveState {
    dl_len: usize,
    next_clip_index: usize,
    next_spatial_index: usize,
    next_clip_chain_id: u64,
}

#[derive(Clone)]
pub struct DisplayListBuilder {
    pub data: Vec<u8>,
    pub pipeline_id: PipelineId,
    next_clip_index: usize,
    next_spatial_index: usize,
    next_clip_chain_id: u64,
    builder_start_time: u64,

    /// The size of the content of this display list. This is used to allow scrolling
    /// outside the bounds of the display list items themselves.
    content_size: LayoutSize,
    save_state: Option<SaveState>,
}

impl DisplayListBuilder {
    pub fn new(pipeline_id: PipelineId, content_size: LayoutSize) -> Self {
        Self::with_capacity(pipeline_id, content_size, 0)
    }

    pub fn with_capacity(
        pipeline_id: PipelineId,
        content_size: LayoutSize,
        capacity: usize,
    ) -> Self {
        let start_time = precise_time_ns();

        DisplayListBuilder {
            data: Vec::with_capacity(capacity),
            pipeline_id,
            next_clip_index: FIRST_CLIP_NODE_INDEX,
            next_spatial_index: FIRST_SPATIAL_NODE_INDEX,
            next_clip_chain_id: 0,
            builder_start_time: start_time,
            content_size,
            save_state: None,
        }
    }

    /// Return the content size for this display list
    pub fn content_size(&self) -> LayoutSize {
        self.content_size
    }

    /// Saves the current display list state, so it may be `restore()`'d.
    ///
    /// # Conditions:
    ///
    /// * Doesn't support popping clips that were pushed before the save.
    /// * Doesn't support nested saves.
    /// * Must call `clear_save()` if the restore becomes unnecessary.
    pub fn save(&mut self) {
        assert!(self.save_state.is_none(), "DisplayListBuilder doesn't support nested saves");

        self.save_state = Some(SaveState {
            dl_len: self.data.len(),
            next_clip_index: self.next_clip_index,
            next_spatial_index: self.next_spatial_index,
            next_clip_chain_id: self.next_clip_chain_id,
        });
    }

    /// Restores the state of the builder to when `save()` was last called.
    pub fn restore(&mut self) {
        let state = self.save_state.take().expect("No save to restore DisplayListBuilder from");

        self.data.truncate(state.dl_len);
        self.next_clip_index = state.next_clip_index;
        self.next_spatial_index = state.next_spatial_index;
        self.next_clip_chain_id = state.next_clip_chain_id;
    }

    /// Discards the builder's save (indicating the attempted operation was successful).
    pub fn clear_save(&mut self) {
        self.save_state.take().expect("No save to clear in DisplayListBuilder");
    }

    /// Print the display items in the list to stdout.
    pub fn print_display_list(&mut self) {
        self.emit_display_list(0, Range { start: None, end: None }, stdout());
    }

    /// Emits a debug representation of display items in the list, for debugging
    /// purposes. If the range's start parameter is specified, only display
    /// items starting at that index (inclusive) will be printed. If the range's
    /// end parameter is specified, only display items before that index
    /// (exclusive) will be printed. Calling this function with end <= start is
    /// allowed but is just a waste of CPU cycles. The function emits the
    /// debug representation of the selected display items, one per line, with
    /// the given indent, to the provided sink object. The return value is
    /// the total number of items in the display list, which allows the
    /// caller to subsequently invoke this function to only dump the newly-added
    /// items.
    pub fn emit_display_list<W>(
        &mut self,
        indent: usize,
        range: Range<Option<usize>>,
        mut sink: W,
    ) -> usize
    where
        W: Write
    {
        let mut temp = BuiltDisplayList::default();
        mem::swap(&mut temp.data, &mut self.data);

        let mut index: usize = 0;
        {
            let mut iter = BuiltDisplayListIter::new(&temp);
            while let Some(item) = iter.next_raw() {
                if index >= range.start.unwrap_or(0) && range.end.map_or(true, |e| index < e) {
                    writeln!(sink, "{}{:?}", "  ".repeat(indent), item.display_item()).unwrap();
                }
                index += 1;
            }
        }

        self.data = temp.data;
        index
    }

    /// Add an item to the display list.
    ///
    /// NOTE: It is usually preferable to use the specialized methods to push
    /// display items. Pushing unexpected or invalid items here may
    /// result in WebRender panicking or behaving in unexpected ways.
    #[inline]
    pub fn push_item(
        &mut self,
        item: &di::SpecificDisplayItem,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
    ) {
        serialize_fast(
            &mut self.data,
            di::SerializedDisplayItem {
                item,
                layout,
                space_and_clip,
            },
        )
    }

    #[inline]
    fn push_new_empty_item(&mut self, item: &di::SpecificDisplayItem) {
        let pipeline_id = self.pipeline_id;
        self.push_item(
            item,
            &di::LayoutPrimitiveInfo::new(LayoutRect::zero()),
            &di::SpaceAndClipInfo::root_scroll(pipeline_id),
        )
    }

    fn push_iter_impl<I>(data: &mut Vec<u8>, iter_source: I)
    where
        I: IntoIterator,
        I::IntoIter: ExactSizeIterator + Clone,
        I::Item: Serialize,
    {
        let iter = iter_source.into_iter();
        let len = iter.len();
        // Format:
        // payload_byte_size: usize, item_count: usize, [I; item_count]

        // We write a dummy value so there's room for later
        let byte_size_offset = data.len();
        serialize_fast(data, &0usize);
        serialize_fast(data, &len);
        let payload_offset = data.len();

        let count = serialize_iter_fast(data, iter);

        // Now write the actual byte_size
        let final_offset = data.len();
        let byte_size = final_offset - payload_offset;

        // Note we don't use serialize_fast because we don't want to change the Vec's len
        bincode::serialize_into(
            &mut &mut data[byte_size_offset..],
            &byte_size,
        ).unwrap();

        debug_assert_eq!(len, count);
    }

    /// Push items from an iterator to the display list.
    ///
    /// NOTE: Pushing unexpected or invalid items to the display list
    /// may result in panic and confusion.
    pub fn push_iter<I>(&mut self, iter: I)
    where
        I: IntoIterator,
        I::IntoIter: ExactSizeIterator + Clone,
        I::Item: Serialize,
    {
        Self::push_iter_impl(&mut self.data, iter);
    }

    pub fn push_rect(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        color: ColorF,
    ) {
        let item = di::SpecificDisplayItem::Rectangle(di::RectangleDisplayItem { color });
        self.push_item(&item, layout, space_and_clip);
    }

    pub fn push_clear_rect(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
    ) {
        self.push_item(&di::SpecificDisplayItem::ClearRectangle, layout, space_and_clip);
    }

    pub fn push_line(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        wavy_line_thickness: f32,
        orientation: di::LineOrientation,
        color: &ColorF,
        style: di::LineStyle,
    ) {
        let item = di::SpecificDisplayItem::Line(di::LineDisplayItem {
            wavy_line_thickness,
            orientation,
            color: *color,
            style,
        });

        self.push_item(&item, layout, space_and_clip);
    }

    pub fn push_image(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        stretch_size: LayoutSize,
        tile_spacing: LayoutSize,
        image_rendering: di::ImageRendering,
        alpha_type: di::AlphaType,
        key: ImageKey,
        color: ColorF,
    ) {
        let item = di::SpecificDisplayItem::Image(di::ImageDisplayItem {
            image_key: key,
            stretch_size,
            tile_spacing,
            image_rendering,
            alpha_type,
            color,
        });

        self.push_item(&item, layout, space_and_clip);
    }

    /// Push a yuv image. All planar data in yuv image should use the same buffer type.
    pub fn push_yuv_image(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        yuv_data: di::YuvData,
        color_depth: ColorDepth,
        color_space: di::YuvColorSpace,
        image_rendering: di::ImageRendering,
    ) {
        let item = di::SpecificDisplayItem::YuvImage(di::YuvImageDisplayItem {
            yuv_data,
            color_depth,
            color_space,
            image_rendering,
        });
        self.push_item(&item, layout, space_and_clip);
    }

    pub fn push_text(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        glyphs: &[GlyphInstance],
        font_key: FontInstanceKey,
        color: ColorF,
        glyph_options: Option<GlyphOptions>,
    ) {
        let item = di::SpecificDisplayItem::Text(di::TextDisplayItem {
            color,
            font_key,
            glyph_options,
        });

        for split_glyphs in glyphs.chunks(MAX_TEXT_RUN_LENGTH) {
            self.push_item(&item, layout, space_and_clip);
            self.push_iter(split_glyphs);
        }
    }

    /// NOTE: gradients must be pushed in the order they're created
    /// because create_gradient stores the stops in anticipation.
    pub fn create_gradient(
        &mut self,
        start_point: LayoutPoint,
        end_point: LayoutPoint,
        stops: Vec<di::GradientStop>,
        extend_mode: di::ExtendMode,
    ) -> di::Gradient {
        let mut builder = GradientBuilder::with_stops(stops);
        let gradient = builder.gradient(start_point, end_point, extend_mode);
        self.push_stops(builder.stops());
        gradient
    }

    /// NOTE: gradients must be pushed in the order they're created
    /// because create_gradient stores the stops in anticipation.
    pub fn create_radial_gradient(
        &mut self,
        center: LayoutPoint,
        radius: LayoutSize,
        stops: Vec<di::GradientStop>,
        extend_mode: di::ExtendMode,
    ) -> di::RadialGradient {
        let mut builder = GradientBuilder::with_stops(stops);
        let gradient = builder.radial_gradient(center, radius, extend_mode);
        self.push_stops(builder.stops());
        gradient
    }

    pub fn push_border(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        widths: LayoutSideOffsets,
        details: di::BorderDetails,
    ) {
        let item = di::SpecificDisplayItem::Border(di::BorderDisplayItem { details, widths });

        self.push_item(&item, layout, space_and_clip);
    }

    pub fn push_box_shadow(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        box_bounds: LayoutRect,
        offset: LayoutVector2D,
        color: ColorF,
        blur_radius: f32,
        spread_radius: f32,
        border_radius: di::BorderRadius,
        clip_mode: di::BoxShadowClipMode,
    ) {
        let item = di::SpecificDisplayItem::BoxShadow(di::BoxShadowDisplayItem {
            box_bounds,
            offset,
            color,
            blur_radius,
            spread_radius,
            border_radius,
            clip_mode,
        });

        self.push_item(&item, layout, space_and_clip);
    }

    /// Pushes a linear gradient to be displayed.
    ///
    /// The gradient itself is described in the
    /// `gradient` parameter. It is drawn on
    /// a "tile" with the dimensions from `tile_size`.
    /// These tiles are now repeated to the right and
    /// to the bottom infinitely. If `tile_spacing`
    /// is not zero spacers with the given dimensions
    /// are inserted between the tiles as seams.
    ///
    /// The origin of the tiles is given in `layout.rect.origin`.
    /// If the gradient should only be displayed once limit
    /// the `layout.rect.size` to a single tile.
    /// The gradient is only visible within the local clip.
    pub fn push_gradient(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        gradient: di::Gradient,
        tile_size: LayoutSize,
        tile_spacing: LayoutSize,
    ) {
        let item = di::SpecificDisplayItem::Gradient(di::GradientDisplayItem {
            gradient,
            tile_size,
            tile_spacing,
        });

        self.push_item(&item, layout, space_and_clip);
    }

    /// Pushes a radial gradient to be displayed.
    ///
    /// See [`push_gradient`](#method.push_gradient) for explanation.
    pub fn push_radial_gradient(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        gradient: di::RadialGradient,
        tile_size: LayoutSize,
        tile_spacing: LayoutSize,
    ) {
        let item = di::SpecificDisplayItem::RadialGradient(di::RadialGradientDisplayItem {
            gradient,
            tile_size,
            tile_spacing,
        });

        self.push_item(&item, layout, space_and_clip);
    }

    pub fn push_reference_frame(
        &mut self,
        rect: &LayoutRect,
        parent: di::SpatialId,
        transform_style: di::TransformStyle,
        transform: PropertyBinding<LayoutTransform>,
        kind: di::ReferenceFrameKind,
    ) -> di::SpatialId {
        let id = self.generate_spatial_index();

        let item = di::SpecificDisplayItem::PushReferenceFrame(di::ReferenceFrameDisplayListItem {
            reference_frame: di::ReferenceFrame {
                transform_style,
                transform,
                kind,
                id,
            },
        });

        let layout = di::LayoutPrimitiveInfo::new(*rect);
        self.push_item(&item, &layout, &di::SpaceAndClipInfo {
            spatial_id: parent,
            clip_id: di::ClipId::invalid(),
        });
        id
    }

    pub fn push_cache_marker(&mut self) {
        self.push_new_empty_item(&di::SpecificDisplayItem::PushCacheMarker(di::CacheMarkerDisplayItem {
            // The display item itself is empty for now while we experiment with
            // the API. In future it may contain extra information, such as details
            // on whether the surface is known to be opaque and/or a background color
            // hint that WR should clear the surface to.
        }));
    }

    pub fn pop_cache_marker(&mut self) {
        self.push_new_empty_item(&di::SpecificDisplayItem::PopCacheMarker);
    }

    pub fn pop_reference_frame(&mut self) {
        self.push_new_empty_item(&di::SpecificDisplayItem::PopReferenceFrame);
    }

    pub fn push_stacking_context(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        spatial_id: di::SpatialId,
        clip_id: Option<di::ClipId>,
        transform_style: di::TransformStyle,
        mix_blend_mode: di::MixBlendMode,
        filters: &[di::FilterOp],
        filter_datas: &[di::FilterData],
        raster_space: di::RasterSpace,
        cache_tiles: bool,
    ) {
        self.push_new_empty_item(&di::SpecificDisplayItem::SetFilterOps);
        self.push_iter(filters);

        for filter_data in filter_datas {
            let func_types = [
                filter_data.func_r_type, filter_data.func_g_type,
                filter_data.func_b_type, filter_data.func_a_type];
            self.push_new_empty_item(&di::SpecificDisplayItem::SetFilterData);
            self.push_iter(&func_types);
            self.push_iter(&filter_data.r_values);
            self.push_iter(&filter_data.g_values);
            self.push_iter(&filter_data.b_values);
            self.push_iter(&filter_data.a_values);
        }

        let item = di::SpecificDisplayItem::PushStackingContext(di::PushStackingContextDisplayItem {
            stacking_context: di::StackingContext {
                transform_style,
                mix_blend_mode,
                clip_id,
                raster_space,
                cache_tiles,
            },
        });

        self.push_item(&item, layout, &di::SpaceAndClipInfo {
            spatial_id,
            clip_id: di::ClipId::invalid(),
        });
    }

    /// Helper for examples/ code.
    pub fn push_simple_stacking_context(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        spatial_id: di::SpatialId,
    ) {
        self.push_simple_stacking_context_with_filters(layout, spatial_id, &[], &[]);
    }

    /// Helper for examples/ code.
    pub fn push_simple_stacking_context_with_filters(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        spatial_id: di::SpatialId,
        filters: &[di::FilterOp],
        filter_datas: &[di::FilterData],
    ) {
        self.push_stacking_context(
            layout,
            spatial_id,
            None,
            di::TransformStyle::Flat,
            di::MixBlendMode::Normal,
            filters,
            filter_datas,
            di::RasterSpace::Screen,
            /* cache_tiles = */ false,
        );
    }

    pub fn pop_stacking_context(&mut self) {
        self.push_new_empty_item(&di::SpecificDisplayItem::PopStackingContext);
    }

    pub fn push_stops(&mut self, stops: &[di::GradientStop]) {
        if stops.is_empty() {
            return;
        }
        self.push_new_empty_item(&di::SpecificDisplayItem::SetGradientStops);
        self.push_iter(stops);
    }

    fn generate_clip_index(&mut self) -> di::ClipId {
        self.next_clip_index += 1;
        di::ClipId::Clip(self.next_clip_index - 1, self.pipeline_id)
    }

    fn generate_spatial_index(&mut self) -> di::SpatialId {
        self.next_spatial_index += 1;
        di::SpatialId::new(self.next_spatial_index - 1, self.pipeline_id)
    }

    fn generate_clip_chain_id(&mut self) -> di::ClipChainId {
        self.next_clip_chain_id += 1;
        di::ClipChainId(self.next_clip_chain_id - 1, self.pipeline_id)
    }

    pub fn define_scroll_frame<I>(
        &mut self,
        parent_space_and_clip: &di::SpaceAndClipInfo,
        external_id: Option<di::ExternalScrollId>,
        content_rect: LayoutRect,
        clip_rect: LayoutRect,
        complex_clips: I,
        image_mask: Option<di::ImageMask>,
        scroll_sensitivity: di::ScrollSensitivity,
        external_scroll_offset: LayoutVector2D,
    ) -> di::SpaceAndClipInfo
    where
        I: IntoIterator<Item = di::ComplexClipRegion>,
        I::IntoIter: ExactSizeIterator + Clone,
    {
        let clip_id = self.generate_clip_index();
        let scroll_frame_id = self.generate_spatial_index();
        let item = di::SpecificDisplayItem::ScrollFrame(di::ScrollFrameDisplayItem {
            clip_id,
            scroll_frame_id,
            external_id,
            image_mask,
            scroll_sensitivity,
            external_scroll_offset,
        });

        self.push_item(
            &item,
            &di::LayoutPrimitiveInfo::with_clip_rect(content_rect, clip_rect),
            parent_space_and_clip,
        );
        self.push_iter(complex_clips);

        di::SpaceAndClipInfo {
            spatial_id: scroll_frame_id,
            clip_id,
        }
    }

    pub fn define_clip_chain<I>(
        &mut self,
        parent: Option<di::ClipChainId>,
        clips: I,
    ) -> di::ClipChainId
    where
        I: IntoIterator<Item = di::ClipId>,
        I::IntoIter: ExactSizeIterator + Clone,
    {
        let id = self.generate_clip_chain_id();
        self.push_new_empty_item(&di::SpecificDisplayItem::ClipChain(di::ClipChainItem { id, parent }));
        self.push_iter(clips);
        id
    }

    pub fn define_clip<I>(
        &mut self,
        parent_space_and_clip: &di::SpaceAndClipInfo,
        clip_rect: LayoutRect,
        complex_clips: I,
        image_mask: Option<di::ImageMask>,
    ) -> di::ClipId
    where
        I: IntoIterator<Item = di::ComplexClipRegion>,
        I::IntoIter: ExactSizeIterator + Clone,
    {
        let id = self.generate_clip_index();
        let item = di::SpecificDisplayItem::Clip(di::ClipDisplayItem {
            id,
            image_mask,
        });

        self.push_item(
            &item,
            &di::LayoutPrimitiveInfo::new(clip_rect),
            parent_space_and_clip,
        );
        self.push_iter(complex_clips);
        id
    }

    pub fn define_sticky_frame(
        &mut self,
        parent_spatial_id: di::SpatialId,
        frame_rect: LayoutRect,
        margins: SideOffsets2D<Option<f32>>,
        vertical_offset_bounds: di::StickyOffsetBounds,
        horizontal_offset_bounds: di::StickyOffsetBounds,
        previously_applied_offset: LayoutVector2D,
    ) -> di::SpatialId {
        let id = self.generate_spatial_index();
        let item = di::SpecificDisplayItem::StickyFrame(di::StickyFrameDisplayItem {
            id,
            margins,
            vertical_offset_bounds,
            horizontal_offset_bounds,
            previously_applied_offset,
        });

        self.push_item(
            &item,
            &di::LayoutPrimitiveInfo::new(frame_rect),
            &di::SpaceAndClipInfo {
                spatial_id: parent_spatial_id,
                clip_id: di::ClipId::invalid(),
            },
        );
        id
    }

    pub fn push_iframe(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        pipeline_id: PipelineId,
        ignore_missing_pipeline: bool
    ) {
        let item = di::SpecificDisplayItem::Iframe(di::IframeDisplayItem {
            pipeline_id,
            ignore_missing_pipeline,
        });
        self.push_item(&item, layout, space_and_clip);
    }

    pub fn push_shadow(
        &mut self,
        layout: &di::LayoutPrimitiveInfo,
        space_and_clip: &di::SpaceAndClipInfo,
        shadow: di::Shadow,
    ) {
        self.push_item(&di::SpecificDisplayItem::PushShadow(shadow), layout, space_and_clip);
    }

    pub fn pop_all_shadows(&mut self) {
        self.push_new_empty_item(&di::SpecificDisplayItem::PopAllShadows);
    }

    pub fn finalize(self) -> (PipelineId, LayoutSize, BuiltDisplayList) {
        assert!(self.save_state.is_none(), "Finalized DisplayListBuilder with a pending save");

        let end_time = precise_time_ns();

        (
            self.pipeline_id,
            self.content_size,
            BuiltDisplayList {
                descriptor: BuiltDisplayListDescriptor {
                    builder_start_time: self.builder_start_time,
                    builder_finish_time: end_time,
                    send_start_time: 0,
                    total_clip_nodes: self.next_clip_index,
                    total_spatial_nodes: self.next_spatial_index,
                },
                data: self.data,
            },
        )
    }
}

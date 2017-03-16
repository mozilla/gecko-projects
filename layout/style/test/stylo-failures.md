This file describes test failures we currently have for stylo.

Failure patterns are described in the following format:
* test_name.html [number]
* test_name.html `pattern` [number]
* test_name.html: description [number]
* test_name.html `pattern`: description [number]
* test_name.html asserts [number]
* test_name.html asserts: description [number]

In which
* test_name.html is the name of the test. It could be "..." which means
  the same name as the previous failure pattern.
* description is a comment for the supposed reason of the failure.
* [number] is the expected count of the failure, which can be a "*" meaning
  any positive number.
* `pattern` is a substring of the failure message. If there are multiple items
  for the same test file, a failure is captured by the first matched pattern.
  For example, if there is a failure with message "foo bar", and there is a
  pattern `foo` followed by a pattern `bar`, this failure would be counted in
  the pattern `foo`.
* "asserts" means it is for assertion. Number of assertions of a same test is
  accumulated, unlike `pattern`. And number of assertions cannot be "*".

Any line which doesn't follow the format above would be ignored like comment.

## Failures

* Media query support:
  * test_bug1089417.html [1]
  * test_bug418986-2.html: matchMedia support [3]
  * test_bug453896_deck.html: &lt;style media&gt; support [8]
  * test_media_queries.html [657]
  * test_media_queries_dynamic.html [10]
  * test_media_queries_dynamic_xbl.html [2]
  * test_webkit_device_pixel_ratio.html: -webkit-device-pixel-ratio [3]
  * browser_bug453896.js [8]
  * test_display_mode.html [7]
  * test_display_mode_reflow.html [2]
* test_all_shorthand.html: all shorthand servo/servo#15055 [*]
* Animation support:
  * test_animations.html [35]
  * test_animations_dynamic_changes.html [1]
  * test_bug716226.html [1]
  * OMTA
    * test_animations_effect_timing_duration.html [1]
    * test_animations_effect_timing_enddelay.html [1]
    * test_animations_effect_timing_iterations.html [1]
    * test_animations_iterationstart.html [1]
    * test_animations_omta.html [1]
    * test_animations_omta_start.html [1]
    * test_animations_pausing.html [1]
    * test_animations_playbackrate.html [1]
  * SMIL Animation
    * test_restyles_in_smil_animation.html [2]
  * CSS Timing Functions: Frames timing functions
    * test_value_storage.html `frames` [30]
  * Property parsing and computation:
    * test_property_syntax_errors.html `animation` [404]
    * test_value_storage.html `animation` [280]
* test_any_dynamic.html: -moz-any pseudo class [2]
* CSSOM support:
  * @import
    * test_bug221428.html [1]
  * @media
    * test_condition_text_assignment.html [1]
    * test_css_eof_handling.html [1]
    * test_group_insertRule.html [16]
    * test_rules_out_of_sheets.html [1]
  * @keyframes
    * test_keyframes_rules.html [1]
  * @support
    * test_supports_rules.html [1]
* test_box_size_keywords.html: moz-prefixed intrinsic size keyword value [64]
* test_bug229915.html: sibling selector with dynamic change bug 1330885 [5]
* test_bug357614.html: case-insensitivity for old attrs in attr selector servo/servo#15006 [2]
* mapped attribute not supported
  * test_bug363146.html [2]
  * test_bug389464.html: also font-size computation [1]
  * test_html_attribute_computed_values.html: also list-style-type [8]
* test_bug387615.html: getComputedStyle value not updated bug 1331294 ##important## (when that gets fixed, servo/servo#15006) [1]
* test_bug397427.html: @import issue bug 1331291 and CSSOM support of @import [3]
* console support:
  * test_bug413958.html `monitorConsole` [3]
  * test_parser_diagnostics_unprintables.html [550]
* test_bug511909.html: @-moz-document and @media support [4]
* Style change from DOM API bug 1331301
  * test_bug534804.html [90]
  * test_bug73586.html [20]
* Transition support:
  * test_bug621351.html [4]
  * test_compute_data_with_start_struct.html `transition` [2]
  * test_transitions.html [63]
  * test_transitions_and_reframes.html [16]
  * test_transitions_and_restyles.html [3]
  * test_transitions_computed_value_combinations.html [145]
  * test_transitions_dynamic_changes.html [10]
  * test_transitions_step_functions.html [24]
  * test_value_storage.html `transition` [776]
  * Events:
    * test_animations_event_handler_attribute.html [10]
    * test_animations_event_order.html [11]
* test_bug798843_pref.html: conditional opentype svg support [7]
* test_computed_style.html `gradient`: -moz-prefixed radient value [9]
* url value in style attribute bug 1310886
  * test_computed_style.html `url` [11]
  * test_parse_url.html [4]
  * test_value_storage.html `url` [53]
  * test_shorthand_property_getters.html `url` [3]
* auto value for min-{width,height} servo/servo#15045
* test_compute_data_with_start_struct.html `timing-function`: incorrectly computing keywords to bezier function servo/servo#15086 [2]
* test_condition_text.html: @-moz-document, CSSOM support of @media, @support [7]
* @counter-style support:
  * test_counter_descriptor_storage.html [1]
  * test_counter_style.html [1]
  * test_rule_insertion.html `@counter-style` [4]
  * test_value_storage.html `symbols(` [30]
  * ... `list-style-type` [60]
  * ... `'list-style'` [30]
* test_default_computed_style.html: support of getDefaultComputedStyle [1]
* @font-face support bug 1290237
  * test_descriptor_storage.html [1]
  * test_descriptor_syntax_errors.html [1]
  * test_font_face_parser.html `@font-face` [447]
  * test_redundant_font_download.html [3]
* @namespace support:
  * test_namespace_rule.html [17]
* test_dont_use_document_colors.html: support of disabling document color [21]
* test_exposed_prop_accessors.html: mainly various unsupported properties [*]
* test_extra_inherit_initial.html: CSS-wide keywords are accepted as part of value servo/servo#15054 [980]
* test_flexbox_flex_shorthand.html `flex-basis`: **need investigation** [12]
* test_flexbox_layout.html: **need investigation** [5]
* test_font_feature_values_parsing.html: @font-feature-values support [107]
* Grid support:
  * test_grid_computed_values.html [4]
  * test_grid_container_shorthands.html [65]
  * test_grid_item_shorthands.html [23]
  * test_grid_shorthand_serialization.html [28]
  * test_compute_data_with_start_struct.html `grid-` [*]
  * test_inherit_computation.html `grid` [*]
  * test_inherit_storage.html `'grid` [*]
  * ... `"grid` [*]
  * test_initial_computation.html `grid` [*]
  * test_initial_storage.html `grid` [*]
  * test_property_syntax_errors.html `grid`: actually there are issues with this [*]
  * test_value_storage.html `'grid` [*]
* test_hover_quirk.html: hover quirks [6]
* url value from decl setter bug 1330503
  * test_compute_data_with_start_struct.html `border-image-source` [2]
  * ... `mask-image` [2]
  * test_inherit_computation.html `border-image` [2]
  * ... `mask-image` [4]
  * test_initial_computation.html `border-image` [4]
  * ... `mask-image` [8]
* Unimplemented prefixed properties:
  * -moz-border-*-colors
    * test_compute_data_with_start_struct.html `-colors` [8]
    * test_inherit_computation.html `-colors` [8]
    * test_inherit_storage.html `-colors` [12]
    * test_initial_computation.html `-colors` [16]
    * test_initial_storage.html `-colors` [24]
    * test_value_storage.html `-colors` [96]
    * test_shorthand_property_getters.html `-colors` [1]
  * -moz-box-{direction,ordinal-group,orient,pack}
    * test_compute_data_with_start_struct.html `-moz-box-` [2]
    * test_inherit_computation.html `-box-` [4]
    * test_inherit_storage.html `-box-` [5]
    * test_initial_computation.html `-box-` [8]
    * test_initial_storage.html `-box-` [10]
    * test_value_storage.html `-box-` [118]
  * -moz-force-broken-image-icon
    * test_compute_data_with_start_struct.html `-moz-force-broken-image-icon` [2]
    * test_inherit_computation.html `-moz-force-broken-image-icon` [2]
    * test_inherit_storage.html `-moz-force-broken-image-icon` [2]
    * test_initial_computation.html `-moz-force-broken-image-icon` [4]
    * test_initial_storage.html `-moz-force-broken-image-icon` [4]
    * test_value_storage.html `-moz-force-broken-image-icon` [4]
  * -{moz,webkit}-text-size-adjust
    * test_compute_data_with_start_struct.html `-text-size-adjust` [2]
    * test_inherit_computation.html `-text-size-adjust` [8]
    * test_inherit_storage.html `-text-size-adjust` [10]
    * test_initial_computation.html `-text-size-adjust` [4]
    * test_initial_storage.html `-text-size-adjust` [5]
    * test_value_storage.html `-text-size-adjust` [12]
  * -moz-transform: need different parsing rules
    * test_inherit_computation.html `-moz-transform`: need different parsing rules [2]
    * test_inherit_storage.html `transform`: for -moz-transform [3]
    * test_initial_computation.html `-moz-transform`: need different parsing rules [4]
    * test_initial_storage.html `transform`: for -moz-transform [6]
    * test_value_storage.html `-moz-transform`: need different parsing rules [284]
  * test_variables.html `var(--var6)`: -x-system-font [1]
* Unimplemented CSS properties:
  * will-change longhand property
    * test_change_hint_optimizations.html [1]
    * test_compute_data_with_start_struct.html `will-change` [2]
    * test_inherit_computation.html `will-change` [2]
    * test_inherit_storage.html `will-change` [2]
    * test_initial_computation.html `will-change` [4]
    * test_initial_storage.html `will-change` [4]
    * test_value_storage.html `will-change` [18]
  * contain longhand property
    * test_contain_formatting_context.html [1]
    * test_compute_data_with_start_struct.html `contain` [2]
    * test_inherit_computation.html `contain` [2]
    * test_inherit_storage.html `contain` [2]
    * test_initial_computation.html `contain` [4]
    * test_initial_storage.html `contain` [4]
    * test_value_storage.html `'contain'` [30]
  * flexbox / grid position properties **need investigation**
    * test_inherit_storage.html `align-` [3]
    * ... `justify-` [3]
    * test_initial_storage.html `align-` [6]
    * ... `justify-` [6]
    * test_value_storage.html `align-` [90]
    * ... `justify-` [79]
  * place-{content,items,self} shorthands
    * test_align_shorthand_serialization.html [8]
    * test_inherit_computation.html `place-` [6]
    * test_inherit_storage.html `place-` [6]
    * test_initial_computation.html `place-` [12]
    * test_initial_storage.html `place-` [12]
    * test_value_storage.html `place-` [66]
  * caret-color servo/servo#15309
    * test_compute_data_with_start_struct.html `caret-color` [2]
    * test_inherit_computation.html `caret-color` [4]
    * test_inherit_storage.html `caret-color` [4]
    * test_initial_computation.html `caret-color` [2]
    * test_initial_storage.html `caret-color` [2]
    * test_value_storage.html `caret-color` [16]
  * font-variant-{alternates,east-asian,ligatures,numeric} properties
    * test_compute_data_with_start_struct.html `font-variant` [8]
    * test_inherit_computation.html `font-variant` [20]
    * test_inherit_storage.html `font-variant` [36]
    * test_initial_computation.html `font-variant` [10]
    * test_initial_storage.html `font-variant` [18]
    * test_value_storage.html `font-variant` [332]
  * initial-letter property
    * test_compute_data_with_start_struct.html `initial-letter` [2]
    * test_inherit_computation.html `initial-letter` [2]
    * test_inherit_storage.html `initial-letter` [2]
    * test_initial_computation.html `initial-letter` [4]
    * test_initial_storage.html `initial-letter` [4]
    * test_value_storage.html `initial-letter` [10]
  * shape-outside property
    * test_compute_data_with_start_struct.html `shape-outside` [2]
    * test_inherit_computation.html `shape-outside` [2]
    * test_inherit_storage.html `shape-outside` [2]
    * test_initial_computation.html `shape-outside` [4]
    * test_initial_storage.html `shape-outside` [4]
    * test_value_storage.html `shape-outside` [121]
  * touch-action property
    * test_compute_data_with_start_struct.html `touch-action` [2]
    * test_inherit_computation.html `touch-action` [2]
    * test_inherit_storage.html `touch-action` [2]
    * test_initial_computation.html `touch-action` [4]
    * test_initial_storage.html `touch-action` [4]
    * test_value_storage.html `touch-action` [14]
* Unimplemented SVG properties:
  * stroke properties
    * test_value_storage.html `on 'stroke` [6]
    * test_compute_data_with_start_struct.html `initial and other values of stroke-dasharray are different` [2]
* Properties implemented but not in geckolib:
  * counter-reset property:
    * test_bug829816.html [8]
    * test_value_storage.html `counter-reset` [2]
  * counter-increment property:
    * test_value_storage.html `counter-increment` [2]
  * clip property: servo/servo#15729
    * test_value_storage.html `should be idempotent for 'clip` [4]
  * font-feature-settings property
    * test_compute_data_with_start_struct.html `font-feature-settings` [2]
    * test_inherit_computation.html `font-feature-settings` [8]
    * test_inherit_storage.html `font-feature-settings` [12]
    * test_initial_computation.html `font-feature-settings` [4]
    * test_initial_storage.html `font-feature-settings` [6]
    * test_value_storage.html `font-feature-settings` [112]
  * font-language-override property
    * test_compute_data_with_start_struct.html `font-language-override` [2]
    * test_inherit_computation.html `font-language-override` [8]
    * test_inherit_storage.html `font-language-override` [12]
    * test_initial_computation.html `font-language-override` [4]
    * test_initial_storage.html `font-language-override` [6]
    * test_value_storage.html `font-language-override` [58]
  * image-orientation property
    * test_compute_data_with_start_struct.html `image-orientation` [2]
    * test_inherit_computation.html `image-orientation` [4]
    * test_inherit_storage.html `image-orientation` [4]
    * test_initial_computation.html `image-orientation` [2]
    * test_initial_storage.html `image-orientation` [2]
    * test_value_storage.html `image-orientation` [80]
* @page support
  * test_bug887741_at-rules_in_declaration_lists.html [1]
  * test_page_parser.html [30]
  * test_rule_insertion.html `@page` [4]
* Unsupported prefixed values
  * moz-prefixed gradient functions bug 1337655
    * test_value_storage.html `-moz-linear-gradient` [293]
    * ... `-moz-radial-gradient` [309]
    * ... `-moz-repeating-` [298]
  * webkit-prefixed gradient functions servo/servo#15441
    * test_value_storage.html `-webkit-gradient` [225]
    * ... `-webkit-linear-gradient` [40]
    * ... `-webkit-radial-gradient` [105]
    * ... `-webkit-repeating-` [35]
  * moz-prefixed intrinsic width values
    * test_flexbox_flex_shorthand.html `-moz-fit-content` [2]
    * test_value_storage.html `-moz-max-content` [52]
    * ... `-moz-min-content` [12]
    * ... `-moz-fit-content` [12]
    * ... `-moz-available` [10]
  * -moz-element() function for &lt;image&gt;
    * test_value_storage.html `-moz-element` [49]
  * -moz-anchor-decoration value on text-decoration
    * test_value_storage.html `-moz-anchor-decoration` [10]
  * several prefixed values in cursor property
    * test_value_storage.html `cursor` [4]
  * moz-prefixed values of overflow shorthand bug 1330888
    * test_bug319381.html [8]
    * test_value_storage.html `'overflow` [8]
  * -moz-middle-with-baseline on vertical-align
    * test_value_storage.html `-moz-middle-with-baseline` [1]
  * -moz-pre-space on white-space
    * test_value_storage.html `-moz-pre-space` [1]
  * -moz-crisp-edges on image-rendering
    * test_value_storage.html `-moz-crisp-edges` [1]
  * -webkit-{flex,inline-flex} for display servo/servo#15400
    * test_webkit_flex_display.html [4]
  * test_pixel_lengths.html `mozmm`: mozmm unit [3]
* Unsupported values
  * SVG-only values of pointer-events not recognized
    * test_compute_data_with_start_struct.html `pointer-events` [2]
    * test_inherit_computation.html `pointer-events` [4]
    * test_initial_computation.html `pointer-events` [2]
    * test_pointer-events.html [2]
    * test_value_storage.html `pointer-events` [8]
  * new syntax of rgba?() and hsla?() functions servo/rust-cssparser#113
    * test_value_storage.html `'color'` [35]
    * ... `rgb(100, 100.0, 100)` [1]
  * color interpolation hint not supported servo/servo#15166
    * test_value_storage.html `'linear-gradient` [50]
  * two-keyword form of background-repeat/mask-repeat servo/servo#14954
    * test_value_storage.html `background-repeat` [14]
    * ... `mask-repeat` [24]
  * lack glue for function values of content property bug 1296477
    * test_rule_insertion.html `decimal counter` [1]
    * test_value_storage.html `'content` [12]
  * SVG-in-OpenType values not supported servo/servo#15211
    * test_value_storage.html `context-` [2]
  * writing-mode: sideways-{lr,rl} and SVG values servo/servo#15213
    * test_logical_properties.html `sideways` [1224]
    * test_value_storage.html `writing-mode` [8]
* Incorrect parsing
  * calc() doesn't support dividing expression servo/servo#15192
    * test_value_storage.html `calc(50px/` [7]
    * ... `calc(2em / ` [9]
  * calc() doesn't support number value servo/servo#15205
    * test_value_storage.html `calc(-2.5)` [1]
  * size part of shorthand background/mask always desires two values servo/servo#15199
    * test_value_storage.html `'background'` [18]
    * ... `/ auto none` [34]
    * ... `/ auto repeat` [17]
  * border shorthands do not reset border-image servo/servo#15202
    * test_inherit_storage.html `for property 'border-image-` [5]
    * test_initial_storage.html `for property 'border-image-` [10]
    * test_value_storage.html `(for 'border-image-` [60]
  * -moz-alt-content parsing is wrong: servo/servo#15726
    * test_property_syntax_errors.html `-moz-alt-content` [4]
  * {transform,perspective}-origin fail to parse 'center left' and 'center right' servo/servo#15750
    * test_value_storage.html `'center left'` [8]
    * ... `'center right'` [8]
* Incorrect parsing
  * border-image and -moz-border-image shorthand servo/servo#15770
    * test_property_syntax_errors.html `border-image` [4]
  * mask shorthand servo/servo#15772
    * test_property_syntax_errors.html `mask'` [76]
* Incorrect serialization
  * border-radius and -moz-outline-radius shorthand servo/servo#15169
    * test_priority_preservation.html `border-radius` [4]
    * test_value_storage.html `border-radius:` [156]
    * ... `-moz-outline-radius:` [76]
    * test_shorthand_property_getters.html `should condense to shortest possible` [6]
  * transform property servo/servo#15194
    * test_value_storage.html `'transform` [104]
    * ... `"transform` [66]
    * ... `-webkit-transform` [109]
    * test_specified_value_serialization.html [27]
    * test_units_angle.html [3]
  * test_value_storage.html `columns:`: **need investigation** [20]
  * {background,mask}-position lacks comma for serialization servo/servo#15200
    * test_value_storage.html `background-position` [81]
    * ... `for 'mask-position` [94]
    * ... `for '-webkit-mask-position` [188]
    * ... `for '-webkit-mask` [38]
    * test_shorthand_property_getters.html `background-position` [1]
  * box-shadow wrong order of &lt;length&gt; values servo/servo#15203
    * test_value_storage.html `box-shadow` [44]
  * color value not canonicalized servo/servo#15397
    * test_shorthand_property_getters.html `should condense to canonical case` [2]
  * background-position invalid 3-value form **issue to be filed**
    * test_shorthand_property_getters.html `should serialize to 4-value` [2]
  * test_variables.html `--weird`: name of custom property is not escaped properly servo/servo#15399 [1]
  * ... `got "--`: CSS-wide keywords in custom properties servo/servo#15401 [3]
* Unsupported pseudo-classes
  * :default ##easy##
    * test_bug302186.html [24]
  * test_bug98997.html: pseudo-class :empty and :-moz-only-whitespace bug 1337068 [75]
  * :-moz-{first,last}-node
    * test_selectors.html `:-moz-` [4]
    * ... `unexpected rule index` [2]
  * :placeholder-shown
    * test_selectors.html `TypeError` [1]
* issues arround font shorthand servo/servo#15032 servo/servo#15036
  * test_bug377947.html [2]
  * test_value_storage.html `'font'` [144]
  * test_shorthand_property_getters.html `font shorthand` [2]
  * test_system_font_serialization.html [10]
* test_value_storage.html `font-size: calc(`: clamp negative value servo/servo#15296 [3]
* rounding issue
  * test_value_storage.html `33.5833px` [2]
  * ... `0.766667px` [2]
  * ... `105.333px` [2]
* test_value_storage.html `: var(--a)`: extra whitespace is added for shorthand with variables servo/servo#15295 [*]
* border-width computed wrong bug 1335990
  * test_parse_rule.html `border-style` [4]
  * test_initial_computation.html `0px", expected "3px` [48]
* Negative value should be rejected
  * test_property_syntax_errors.html `transition-duration`: servo/servo#15343 [20]
  * ... `perspective'`: servo/servo#15337 [20]
  * third length of shadow servo/servo#15490
    * test_property_syntax_errors.html `box-shadow'` [0]
    * ... `'text-shadow'` [2]
    * ... `drop-shadow` [0]
  * test_property_syntax_errors.html `flex-basis`: servo/servo#15902 [6]
* Quirks mode support
  * hashless color servo/servo#15341
    * test_property_syntax_errors.html `color: 000000` [22]
    * ... `color: 96ed2a` [22]
    * ... `color: fff` [4]
  * unitless length servo/servo#15342
    * test_property_syntax_errors.html ` 20 ` [6]
    * ... `: 10 ` [6]
    * ... ` 2 ` [26]
    * ... `: 5 ` [84]
    * ... `border-spacing: ` [6]
    * ... `rect(1, ` [2]
* test_pseudoelement_parsing.html: support parsing some pseudo-classes on some pseudo-elements [5]
* Unit should be preserved after parsing servo/servo#15346
  * test_units_length.html [5]
  * test_units_time.html [1]
* insertRule / deleteRule don't work bug 1336863
  * test_rule_insertion.html [8]
* @-moz-document support
  * test_rule_serialization.html [2]
  * test_moz_document_rules.html [13]
* getComputedStyle style doesn't contain custom properties bug 1336891
  * test_variable_serialization_computed.html [35]
  * test_variables.html `custom property name` [2]
* test_css_supports.html: issues around @supports syntax servo/servo#15482 [8]
* test_author_specified_style.html: support serializing color as author specified [33]
* browser_newtab_share_rule_processors.js: agent style sheet sharing [1]

## Assertions

* Content glue not implemented
  * assertion in computed style bug 1337635
  * assertion for unsupported or non-animatable property bug 1343710
    * test_animations.html asserts [12]

## Need Gecko change

* Servo is correct but Gecko is wrong
  * unitless zero as angle bug 1234357
    * test_property_syntax_errors.html `linear-gradient(0,` [8]
    * ... `hue-rotate(0)` [6]

## Spec Unclear

* test_property_syntax_errors.html `'background'`: whether background shorthand should accept "text" [200]

## Unknown / Unsure

* test_additional_sheets.html: one sub-test cascade order is wrong [1]
* test_selectors.html `:nth-child`: &lt;an+b&gt; parsing difference [14]
* test_selectors_on_anonymous_content.html: xbl and :nth-child [1]
* test_variables.html `url`: url in custom property [1]
* test_pseudoelement_state.html: doesn't seem to work at all, but only range-thumb fails... [4]
* test_parse_rule.html `rgb(0, 128, 0)`: color properties not getting computed [8]

## Ignore

* Ignore for now since should be mostly identical to test_value_storage.html
  * test_value_cloning.html [*]
  * test_value_computation.html [*]

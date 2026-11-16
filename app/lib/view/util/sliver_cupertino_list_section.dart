/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';

const double _kInsetAdditionalDividerMarginWithoutLeading = 14.0;
const double _kInsetAdditionalDividerMargin = 42.0;
const double _kInsetDividerMargin = 14.0;
const BorderRadius _kDefaultInsetGroupedBorderRadius = BorderRadius.all(
  Radius.circular(10.0),
);
const EdgeInsetsDirectional _kDefaultInsetGroupedRowsMarginWithHeader =
    EdgeInsetsDirectional.fromSTEB(20.0, 0.0, 20.0, 10.0);
const EdgeInsetsDirectional _kDefaultInsetGroupedRowsMargin =
    EdgeInsetsDirectional.fromSTEB(20.0, 20.0, 20.0, 10.0);
const EdgeInsetsDirectional _kInsetGroupedDefaultFooterMargin =
    EdgeInsetsDirectional.fromSTEB(20.0, 0.0, 20.0, 10.0);
const EdgeInsetsDirectional _kInsetGroupedDefaultHeaderMargin =
    EdgeInsetsDirectional.fromSTEB(20.0, 16.0, 20.0, 6.0);

/// A lazy, sliver-based version of [CupertinoListSection.insetGrouped].
///
/// The rows are built by [itemBuilder], so this widget can be placed directly
/// in a [CustomScrollView]. The spacing, rounded group background, row
/// separators, and header/footer styles follow the insetGrouped list section.
class SliverCupertinoListSectionInsetGrouped extends StatelessWidget {
  const SliverCupertinoListSectionInsetGrouped({
    super.key,
    required this.itemBuilder,
    required this.itemCount,
    this.decoration,
    this.backgroundColor = CupertinoColors.systemGroupedBackground,
    this.clipBehavior = Clip.hardEdge,
    this.dividerMargin = _kInsetDividerMargin,
    double? additionalDividerMargin,
    bool hasLeading = true,
    this.topMargin,
    this.separatorColor,
    EdgeInsetsGeometry? margin,
    this.footer,
    this.header,
  }) : assert(itemCount >= 0),
       assert(itemCount > 0 || header != null),
       additionalDividerMargin =
           additionalDividerMargin ??
           (hasLeading
               ? _kInsetAdditionalDividerMargin
               : _kInsetAdditionalDividerMarginWithoutLeading),
       margin =
           margin ??
           (header == null
               ? _kDefaultInsetGroupedRowsMargin
               : _kDefaultInsetGroupedRowsMarginWithHeader);

  /// A decoration for the row group. If omitted, the standard inset grouped
  /// background and 10 pixel superellipse corners are used.
  ///
  /// The corner radii of the decoration are also applied to the first and the
  /// last row, so that rows painting an opaque background stay inside the
  /// corners of the group.
  final Decoration? decoration;

  /// The color behind the complete section, including its header and footer.
  final Color backgroundColor;

  /// How the first and the last row are clipped to the corner radii of
  /// [decoration].
  ///
  /// Slivers are painted inside the viewport clip, so this only controls the
  /// per-row clip of the group corners. Use [Clip.none] to keep the corners
  /// painted but leave the rows unclipped, like [CupertinoListSection] does.
  final Clip clipBehavior;

  /// The starting inset of each separator before [additionalDividerMargin].
  final double dividerMargin;

  /// An additional inset for separators, normally used for a leading widget.
  final double additionalDividerMargin;

  /// Retained for source compatibility. Inset grouped sections do not add the
  /// edge-to-edge variant's top margin.
  final double? topMargin;

  /// The color of separators between rows.
  final Color? separatorColor;

  /// Spacing around the rounded row group.
  final EdgeInsetsGeometry margin;

  /// Content displayed below the row group.
  final Widget? footer;

  /// Content displayed above the row group.
  final Widget? header;

  /// Builds a row for the supplied item index.
  final NullableIndexedWidgetBuilder itemBuilder;

  /// Number of rows available to [itemBuilder].
  final int itemCount;

  @override
  Widget build(BuildContext context) {
    final Color dividerColor =
        separatorColor ?? CupertinoColors.separator.resolveFrom(context);
    final double dividerHeight = 1.0 / MediaQuery.devicePixelRatioOf(context);

    final Decoration groupDecoration =
        decoration ??
        ShapeDecoration(
          color: CupertinoColors.secondarySystemGroupedBackground.resolveFrom(
            context,
          ),
          shape: const RoundedSuperellipseBorder(
            borderRadius: _kDefaultInsetGroupedBorderRadius,
          ),
        );

    // A decoration painted behind a sliver is not clipped the way the box
    // based [CupertinoListSection] clips it, so a row painting an opaque
    // background (a pressed [CupertinoListTile], an image, ...) would cover the
    // rounded corners. The corners are applied to the rows instead, and only
    // the first and the last row can ever paint over one.
    final _RowCorners? rowCorners = clipBehavior == Clip.none
        ? null
        : _RowCorners.of(groupDecoration, Directionality.of(context));

    final Widget rows = SliverPadding(
      padding: margin,
      sliver: DecoratedSliver(
        decoration: groupDecoration,
        sliver: SliverList.separated(
          itemCount: itemCount,
          itemBuilder: (BuildContext context, int index) {
            final Widget? row = itemBuilder(context, index);
            if (row == null || rowCorners == null) {
              return row;
            }
            return rowCorners.clipRow(
              row,
              index: index,
              itemCount: itemCount,
              clipBehavior: clipBehavior,
            );
          },
          separatorBuilder: (BuildContext context, int index) {
            return Padding(
              padding: EdgeInsetsDirectional.only(
                start: dividerMargin + additionalDividerMargin,
              ),
              child: SizedBox(
                height: dividerHeight,
                child: ColoredBox(color: dividerColor),
              ),
            );
          },
        ),
      ),
    );

    final TextStyle textStyle = CupertinoTheme.of(context).textTheme.textStyle;
    final List<Widget> slivers = <Widget>[
      if (header != null)
        SliverToBoxAdapter(
          child: Align(
            alignment: AlignmentDirectional.centerStart,
            child: Padding(
              padding: _kInsetGroupedDefaultHeaderMargin,
              child: DefaultTextStyle(
                style: textStyle.merge(
                  const TextStyle(fontSize: 20.0, fontWeight: FontWeight.bold),
                ),
                child: header!,
              ),
            ),
          ),
        ),
      if (itemCount > 0) rows,
      if (footer != null)
        SliverToBoxAdapter(
          child: Align(
            alignment: AlignmentDirectional.centerStart,
            child: Padding(
              padding: _kInsetGroupedDefaultFooterMargin,
              child: DefaultTextStyle(style: textStyle, child: footer!),
            ),
          ),
        ),
    ];

    final Widget content = header == null && footer == null
        ? rows
        : SliverMainAxisGroup(slivers: slivers);

    return DecoratedSliver(
      decoration: BoxDecoration(
        color: CupertinoDynamicColor.resolve(backgroundColor, context),
      ),
      sliver: content,
    );
  }
}

/// The corner radii of a row group, and the kind of clip needed to reproduce
/// them on a single row.
class _RowCorners {
  const _RowCorners({required this.borderRadius, required this.superellipse});

  /// Reads the corner radii drawn by the row group [decoration].
  ///
  /// Returns `null` when the decoration has square corners, in which case the
  /// rows must not be clipped.
  static _RowCorners? of(Decoration decoration, TextDirection textDirection) {
    BorderRadiusGeometry? radii;
    bool superellipse = false;
    if (decoration is ShapeDecoration) {
      final ShapeBorder shape = decoration.shape;
      if (shape is RoundedSuperellipseBorder) {
        radii = shape.borderRadius;
        superellipse = true;
      } else if (shape is RoundedRectangleBorder) {
        radii = shape.borderRadius;
      }
    } else if (decoration is BoxDecoration) {
      radii = decoration.borderRadius;
    }
    if (radii == null) {
      return null;
    }
    final BorderRadius corners = radii.resolve(textDirection);
    if (corners == BorderRadius.zero) {
      return null;
    }
    return _RowCorners(borderRadius: corners, superellipse: superellipse);
  }

  final BorderRadius borderRadius;

  /// Whether the corners follow the squircle (superellipse) shape used by
  /// [RoundedSuperellipseBorder] instead of a plain rounded rectangle.
  final bool superellipse;

  /// Clips [row] to the corners it can paint over.
  ///
  /// Only the first row paints over the top corners and only the last one over
  /// the bottom corners, so the rows in between are returned untouched.
  Widget clipRow(
    Widget row, {
    required int index,
    required int itemCount,
    required Clip clipBehavior,
  }) {
    final bool isFirst = index == 0;
    final bool isLast = index == itemCount - 1;
    if (!isFirst && !isLast) {
      return row;
    }
    final BorderRadius corners = BorderRadius.only(
      topLeft: isFirst ? borderRadius.topLeft : Radius.zero,
      topRight: isFirst ? borderRadius.topRight : Radius.zero,
      bottomLeft: isLast ? borderRadius.bottomLeft : Radius.zero,
      bottomRight: isLast ? borderRadius.bottomRight : Radius.zero,
    );
    return superellipse
        ? ClipRSuperellipse(
            borderRadius: corners,
            clipBehavior: clipBehavior,
            child: row,
          )
        : ClipRRect(
            borderRadius: corners,
            clipBehavior: clipBehavior,
            child: row,
          );
  }
}

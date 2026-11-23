#pragma once

#include "Recovery.h"
#include "core/Commands.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Easing.h"
#include "core/PaletteTools.h"
#include "core/Playback.h"
#include "core/Raster.h"
#include "core/Tilemap.h"
#include "core/Transform.h"
#include "core/Undo.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

// Owns the open document and exposes editing operations to QML.
class Editor : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // The largest width or height a sprite, canvas, grid cell or imported picture can have.
    Q_PROPERTY(int maxImageSize READ maxImageSize CONSTANT)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY canvasSizeChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY canvasSizeChanged)
    // "rgba" | "grayscale" | "indexed"
    Q_PROPERTY(QString colorMode READ colorMode NOTIFY colorModeChanged)
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(int brushSize READ brushSize WRITE setBrushSize NOTIFY brushSizeChanged)
    Q_PROPERTY(bool pixelPerfect READ pixelPerfect WRITE setPixelPerfect NOTIFY toolOptionsChanged)
    Q_PROPERTY(BrushShape brushShape READ brushShape WRITE setBrushShape NOTIFY brushSizeChanged)
    // The angle every brush is turned by, in degrees (-180..180, 0 = as drawn). One setting for all of
    // them; a round brush looks the same at any angle, the square and custom ones turn.
    Q_PROPERTY(int brushAngle READ brushAngle WRITE setBrushAngle NOTIFY brushSizeChanged)
    // Pencil in shading ink: moves pixels along the palette ramp instead of painting.
    Q_PROPERTY(bool shading READ shading WRITE setShading NOTIFY toolOptionsChanged)
    // The custom brush (made by brushFromSelection), in RGBA, and whether it
    // paints its own colors (or its shape in the primary / secondary color).
    Q_PROPERTY(bool hasCustomBrush READ hasCustomBrush NOTIFY customBrushChanged)
    Q_PROPERTY(QImage customBrushImage READ customBrushImage NOTIFY customBrushChanged)
    Q_PROPERTY(QSize customBrushSize READ customBrushSize NOTIFY customBrushChanged)
    Q_PROPERTY(bool brushColors READ brushColors WRITE setBrushColors NOTIFY toolOptionsChanged)
    // Dithering for the drawing tools (pencil, eraser, shapes, fill): the
    // pattern (a core::DitherPattern::Kind), Ordered's density (1..15 of 16),
    // and whether skipped pixels take the other color instead of staying.
    Q_PROPERTY(bool dither READ dither WRITE setDither NOTIFY toolOptionsChanged)
    Q_PROPERTY(int ditherPattern READ ditherPattern WRITE setDitherPattern NOTIFY toolOptionsChanged)
    Q_PROPERTY(int ditherDensity READ ditherDensity WRITE setDitherDensity NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool ditherOtherColor READ ditherOtherColor WRITE setDitherOtherColor NOTIFY toolOptionsChanged)
    // The palette range the shading ink steps along (first, last index);
    // (-1, -1) = the whole palette.
    Q_PROPERTY(QPoint shadeRange READ shadeRange WRITE setShadeRange NOTIFY shadeRangeChanged)
    // Mirror drawing across the canvas's vertical / horizontal center line.
    Q_PROPERTY(bool symmetryHorizontal READ symmetryHorizontal WRITE setSymmetryHorizontal NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool symmetryVertical READ symmetryVertical WRITE setSymmetryVertical NOTIFY toolOptionsChanged)
    // The diagonal axes through symmetryCenter: "/" (45°) and "\" (-45°).
    Q_PROPERTY(bool symmetryRising READ symmetryRising WRITE setSymmetryRising NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool symmetryFalling READ symmetryFalling WRITE setSymmetryFalling NOTIFY toolOptionsChanged)
    // Where the axes cross, in canvas pixels, in half-pixel steps (a whole
    // number: on a pixel edge; .5: through a pixel's middle). Per document,
    // saved in the .aseprite file (a chunk of Smalto's own) when it isn't in the middle; centered on
    // new documents and canvas size changes. With a diagonal axis on, x and y keep the same half-pixel
    // parity (it gives way by half a pixel).
    Q_PROPERTY(QPointF symmetryCenter READ symmetryCenter WRITE setSymmetryCenter NOTIFY symmetryCenterChanged)
    // Radial symmetry: this many copies (2..12; 0 = off) turned round the crossing point, and with
    // symmetryRadialMirror each also mirrored left-right. Copies land on the nearest pixel; with
    // symmetryRadialTurn the square and custom brushes are turned with each copy, else they keep
    // their own orientation.
    Q_PROPERTY(int symmetryRadial READ symmetryRadial WRITE setSymmetryRadial NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool symmetryRadialMirror READ symmetryRadialMirror WRITE setSymmetryRadialMirror NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool symmetryRadialTurn READ symmetryRadialTurn WRITE setSymmetryRadialTurn NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool fillShapes READ fillShapes WRITE setFillShapes NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool fillContiguous READ fillContiguous WRITE setFillContiguous NOTIFY toolOptionsChanged)
    Q_PROPERTY(int fillTolerance READ fillTolerance WRITE setFillTolerance NOTIFY toolOptionsChanged)
    // Lasso in polygon mode: click corners instead of dragging an outline.
    Q_PROPERTY(bool lassoPolygon READ lassoPolygon WRITE setLassoPolygon NOTIFY toolOptionsChanged)
    // A polygon lasso is being placed (Enter closes, Backspace, Esc).
    Q_PROPERTY(bool polygonActive READ polygonActive NOTIFY polygonChanged)
    // The lasso being drawn, in canvas pixels (pixel centers): a freehand
    // drag's points, or a polygon's corners plus the pointer. Empty otherwise.
    Q_PROPERTY(QList<QPointF> lassoPath READ lassoPath NOTIFY lassoPathChanged)
    // While dragging out a shape or a rectangle selection: its size ("12 × 8"),
    // shown by the pointer. Empty otherwise.
    Q_PROPERTY(QString dragInfo READ dragInfo NOTIFY dragInfoChanged)
    Q_PROPERTY(bool wandContiguous READ wandContiguous WRITE setWandContiguous NOTIFY toolOptionsChanged)
    // Fill and magic wand look at every visible layer (the composite), not
    // just the current cel. Either way only the current cel gets the fill.
    Q_PROPERTY(bool sampleMerged READ sampleMerged WRITE setSampleMerged NOTIFY toolOptionsChanged)
    // Tiled view: 0 off, 1 across X, 2 across Y, 3 both. The canvas repeats
    // the sprite around itself; a stroke started on a copy lands in the
    // sprite, and what crosses an edge on a tiled axis comes back on the
    // other side (so seams can be painted over).
    Q_PROPERTY(int tileMode READ tileMode WRITE setTileMode NOTIFY tileModeChanged)
    // What loading a palette does to an indexed sprite: 0 keeps the indices
    // (the same pixels take the new colors, a palette swap), 1 matches colors
    // (each pixel goes to the new palette's nearest color to what it was).
    // Remembered between runs (Main.qml).
    Q_PROPERTY(int paletteLoadMode READ paletteLoadMode WRITE setPaletteLoadMode NOTIFY paletteLoadModeChanged)
    // Text tool: a click starts a text at that point (its top-left); setText
    // shows it floating, like a paste, in textFont and the primary color, so
    // it can be moved and typed on until it lands (commitFloating) or is
    // dropped (cancelFloating). fonts: their names ("5 x 7"), built in.
    Q_PROPERTY(QStringList fonts READ fonts CONSTANT)
    // Gradient tool: drag from where the primary color starts to where the
    // secondary one is reached; across the selection, or the whole cel. The
    // shape is a core::GradientShape (0 linear, 1 radial, 2 reflected, 3 diamond,
    // 4 conical); smooth, or dithered by a pattern (1 Bayer 2 x 2, 2 Bayer 4 x 4,
    // 3 Bayer 8 x 8, 4 blue noise). With gradientRamp it runs through the palette's shade
    // range (the whole palette without one) instead of from the primary to the secondary
    // color. gradientLine: the drag being made, [start, end] in canvas pixels (empty otherwise).
    Q_PROPERTY(int gradientShape READ gradientShape WRITE setGradientShape NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool gradientRamp READ gradientRamp WRITE setGradientRamp NOTIFY toolOptionsChanged)
    Q_PROPERTY(QVariantList gradientLine READ gradientLine NOTIFY gradientLineChanged)
    Q_PROPERTY(int gradientDither READ gradientDither WRITE setGradientDither NOTIFY toolOptionsChanged)
    // gradientRepeat: 0 off, 1 repeat, 2 mirror. gradientCustom: use gradientCustomStops
    // ([{position 0..1, color}], two or more) instead of the colors / palette; gradientStopsText
    // is the same list as text ("0:#aarrggbb;1:#aarrggbb") to remember between runs.
    // gradientEditable: the gradient just drawn can still be changed (editGradient, options,
    // colors) until finishGradient, Esc, another edit or tool.
    Q_PROPERTY(int gradientRepeat READ gradientRepeat WRITE setGradientRepeat NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool gradientCustom READ gradientCustom WRITE setGradientCustom NOTIFY toolOptionsChanged)
    Q_PROPERTY(QVariantList gradientCustomStops READ gradientCustomStops WRITE setGradientCustomStops NOTIFY toolOptionsChanged)
    Q_PROPERTY(QString gradientStopsText READ gradientStopsText WRITE setGradientStopsText NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool gradientEditable READ gradientEditable NOTIFY gradientLineChanged)
    Q_PROPERTY(int textFont READ textFont WRITE setTextFont NOTIFY toolOptionsChanged)
    // Extra pixels between letters and between lines ('\n' starts one); -2..16.
    Q_PROPERTY(int textLetterSpacing READ textLetterSpacing WRITE setTextLetterSpacing NOTIFY toolOptionsChanged)
    Q_PROPERTY(int textLineSpacing READ textLineSpacing WRITE setTextLineSpacing NOTIFY toolOptionsChanged)
    // How shorter lines sit in a text of several: 0 left, 1 center, 2 right.
    Q_PROPERTY(int textAlign READ textAlign WRITE setTextAlign NOTIFY toolOptionsChanged)
    // Where the typing is (code points into the text; set by the text field)
    // and the insertion point on the canvas, in sprite pixels (a 1 pixel wide
    // line one text line tall; empty when no text is being typed).
    Q_PROPERTY(int textCursor READ textCursor WRITE setTextCursor NOTIFY textChanged)
    Q_PROPERTY(QRect textCaret READ textCaret NOTIFY textChanged)
    Q_PROPERTY(bool textActive READ textActive NOTIFY textChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(int wandTolerance READ wandTolerance WRITE setWandTolerance NOTIFY toolOptionsChanged)
    Q_PROPERTY(QRect grid READ grid NOTIFY gridChanged)
    // Select, shapes, slices and moves put their edges on the document grid
    // (see `grid`): a selection or a rectangle covers whole cells, a line ends on
    // a cell corner, a move goes a whole cell at a time. Remembered.
    Q_PROPERTY(bool snapToGrid READ snapToGrid WRITE setSnapToGrid NOTIFY snapToGridChanged)
    // The slices shown at the current frame (hidden ones left out):
    // {index, name, color, bounds, nineSlice, center (relative to bounds),
    // hasPivot, pivot (relative to the bounds' origin)}.
    Q_PROPERTY(QVariantList slices READ slices NOTIFY slicesChanged)
    // Tilemap layers. tilemapLayer: the current layer is one with a tileset.
    // editTiles: tile mode (place / pick tiles) rather than pixel mode, a
    // tool option. The current layer's tileset: its name, tile size, the
    // number the first tile shows as, how many tiles (tile 0 is the empty
    // one), and the tile picked to draw with. tilesetRevision changes with
    // any of it, for bindings that call tileImage().
    Q_PROPERTY(bool tilemapLayer READ tilemapLayer NOTIFY tilesetChanged)
    Q_PROPERTY(bool editTiles READ editTiles WRITE setEditTiles NOTIFY toolOptionsChanged)
    Q_PROPERTY(QString tilesetName READ tilesetName NOTIFY tilesetChanged)
    // The current layer's tileset as an index into tilesetList (-1: not a tilemap layer).
    Q_PROPERTY(int currentTilesetIndex READ currentTilesetIndex NOTIFY tilesetChanged)
    Q_PROPERTY(QSize tileSize READ tileSize NOTIFY tilesetChanged)
    Q_PROPERTY(int tileBaseIndex READ tileBaseIndex NOTIFY tilesetChanged)
    Q_PROPERTY(int tileCount READ tileCount NOTIFY tilesetChanged)
    Q_PROPERTY(int currentTile READ currentTile WRITE setCurrentTile NOTIFY tilesetChanged)
    Q_PROPERTY(int tilesetRevision READ tilesetRevision NOTIFY tilesetChanged)
    // Flips for the tiles placed in tile mode: kTileFlipX / Y / Diagonal bits.
    Q_PROPERTY(int tileFlips READ tileFlips WRITE setTileFlips NOTIFY toolOptionsChanged)
    // Tile mode is on for the current layer (pencil / eraser / eyedropper work on tiles).
    Q_PROPERTY(bool tilesActive READ tilesActive NOTIFY tilesetChanged)
    // Pixel mode on a tilemap layer, what painting over tiles does: 0 Manual
    // (only edits existing tiles, everywhere they're used; empty cells stay
    // empty), 1 Auto (edits tiles in place; an empty cell gets a matching
    // tile or a new one), 2 Stack (each painted cell gets a new tile).
    Q_PROPERTY(int tileEditMode READ tileEditMode WRITE setTileEditMode NOTIFY toolOptionsChanged)
    // Every slice, shown or not at this frame: {index, name, color, nineSlice, hasPivot}, for the
    // slices list. The one picked with the slice tool (-1: none).
    Q_PROPERTY(QVariantList allSlices READ allSlices NOTIFY slicesChanged)
    Q_PROPERTY(int currentSlice READ currentSlice WRITE setCurrentSlice NOTIFY slicesChanged)
    // Slice tool option: an edit on a frame gives the slice its own key
    // there (it changes from that frame on); off, edits change the key in
    // effect.
    Q_PROPERTY(bool sliceKeys READ sliceKeys WRITE setSliceKeys NOTIFY toolOptionsChanged)
    Q_PROPERTY(QRect selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool floating READ floating NOTIFY floatingChanged)
    // The floating pixels were lifted from the layer (Cmd+T, moving a selection), not pasted.
    Q_PROPERTY(bool floatingLifted READ floatingLifted NOTIFY floatingChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(bool canPasteTimeline READ canPasteTimeline NOTIFY clipboardChanged)
    Q_PROPERTY(QColor primaryColor READ primaryColor WRITE setPrimaryColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor secondaryColor READ secondaryColor WRITE setSecondaryColor NOTIFY colorsChanged)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)
    Q_PROPERTY(int currentLayer READ currentLayer WRITE setCurrentLayer NOTIFY currentLayerChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY framesChanged)
    Q_PROPERTY(int currentFrame READ currentFrame WRITE setCurrentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(int frameDuration READ frameDuration WRITE setFrameDuration NOTIFY frameDurationChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    // Tag being played (-1: all frames, or not playing).
    Q_PROPERTY(int playingTag READ playingTag NOTIFY playingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool canRemoveLayer READ canRemoveLayer NOTIFY layersChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    // Open documents (tabs): {name, path, modified} maps, and which one is
    // current. Everything else on Editor is about the current document.
    Q_PROPERTY(QVariantList documents READ documents NOTIFY documentsChanged)
    Q_PROPERTY(int currentDocument READ currentDocument WRITE setCurrentDocument NOTIFY documentsChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(RangeType rangeType READ rangeType NOTIFY rangeChanged)
    Q_PROPERTY(QRect range READ range NOTIFY rangeChanged)
    Q_PROPERTY(bool onionSkin READ onionSkin WRITE setOnionSkin NOTIFY onionChanged)
    Q_PROPERTY(int onionBefore READ onionBefore WRITE setOnionBefore NOTIFY onionChanged)
    Q_PROPERTY(int onionAfter READ onionAfter WRITE setOnionAfter NOTIFY onionChanged)
    Q_PROPERTY(int onionOpacity READ onionOpacity WRITE setOnionOpacity NOTIFY onionChanged)
    Q_PROPERTY(bool onionTint READ onionTint WRITE setOnionTint NOTIFY onionChanged)
    // Draw the onion skin in front of the sprite (faint frames over it), and loop it within the tag the current frame is in.
    Q_PROPERTY(bool onionFront READ onionFront WRITE setOnionFront NOTIFY onionChanged)
    Q_PROPERTY(bool onionLoop READ onionLoop WRITE setOnionLoop NOTIFY onionChanged)
    // Preferences (remembered by Main.qml). Seconds from the last change to
    // the crash recovery copy, 0: off. Grid cell size for new sprites.
    Q_PROPERTY(int autosaveDelay READ autosaveDelay WRITE setAutosaveDelay NOTIFY preferencesChanged)
    // Memory the undo history of each document may use, in MB (0 = unlimited): the oldest steps go first.
    Q_PROPERTY(int undoMemory READ undoMemory WRITE setUndoMemory NOTIFY preferencesChanged)
    Q_PROPERTY(QSize newGridSize READ newGridSize WRITE setNewGridSize NOTIFY preferencesChanged)

public:
    // New tools go at the end: the last tool is remembered as a number.
    enum Tool { Pencil, Eraser, Fill, Eyedropper, Hand, Line, Rectangle, Ellipse, Select, Move, Lasso, MagicWand,
                Blur, Jumble, Text, SliceTool, Gradient,
                // Only on a live layer: draw the emitter's source, place a field.
                SourceTool, FieldTool,
                // Only on a Smear group: its posing handles (SmearHandles.qml).
                PoseTool };
    // Timeline selection: whole frames (all layers) or a block of cels.
    enum RangeType { NoRange, FrameRange, CelRange };
    Q_ENUM(RangeType)
    Q_ENUM(Tool)
    enum BrushShape { SquareBrush, RoundBrush, CustomBrush };
    Q_ENUM(BrushShape)

    explicit Editor(QObject* parent = nullptr);
    ~Editor() override;

    int canvasWidth() const { return m_doc->width(); }
    QString colorMode() const;
    int canvasHeight() const { return m_doc->height(); }

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    int brushSize() const { return m_brushSize; }
    void setBrushSize(int size);
    // Freehand 1px strokes without L-shaped corners (pencil and eraser).
    bool pixelPerfect() const { return m_pixelPerfect; }
    void setPixelPerfect(bool on);
    BrushShape brushShape() const { return m_brushShape; }
    void setBrushShape(BrushShape shape);
    int brushAngle() const { return m_brushAngle; }
    void setBrushAngle(int degrees);
    // Shading ink: each pixel whose color is in the ramp moves one entry along
    // it per stroke (left mouse toward later entries, right toward earlier),
    // clamped at its ends; other pixels stay.
    bool shading() const { return m_shading; }
    void setShading(bool on);
    QPoint shadeRange() const { return m_shadeRange; }
    void setShadeRange(QPoint range);
    // Custom brush: the selected pixels of the current cel (the whole cel
    // without a selection), trimmed to the non-transparent ones, become the
    // brush (handle at the center) and the brush shape switches to it. Not
    // part of the document: it stays across tabs. Fails when there's nothing
    // to take or it's bigger than kMaxCustomBrush on a side. Picks the pencil
    // if the current tool draws without a brush.
    Q_INVOKABLE bool brushFromSelection();
    // Makes `image` (e.g. from BrushLibrary) the custom brush, trimmed to its
    // opaque pixels; false if it has none or is too big.
    Q_INVOKABLE bool setCustomBrush(const QImage& image);
    // A custom brush's semi-transparent pixels are laid over what is there (RGBA sprites) instead of
    // replacing it.
    Q_PROPERTY(bool brushBlend READ brushBlend WRITE setBrushBlend NOTIFY toolOptionsChanged)
    bool brushBlend() const { return m_brushBlend; }
    void setBrushBlend(bool on);

    // Pen dynamics for freehand strokes made with a pen (a mouse always paints at full pressure, no tilt).
    // Pressure (through pressureCurve: x1, y1, x2, y2 of a core::EasingCurve) can set the brush size (down
    // to pressureMinSize percent; built-in brushes), the opacity (pencil and eraser) and the color between
    // the secondary (light) and the primary (firm) color (pencil; the other button swaps them). Opacity and
    // color are smooth (pressureDither 0; RGBA sprites blend, others take the nearest color) or a dither
    // pattern (1 Bayer 2x2, 2 Bayer 4x4, 3 Bayer 8x8, 4 blue noise) that shows more pixels the firmer the
    // press. tiltAngle turns the brush the way the pen leans, on top of brushAngle.
    Q_PROPERTY(bool pressureSize READ pressureSize WRITE setPressureSize NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool pressureOpacity READ pressureOpacity WRITE setPressureOpacity NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool pressureColor READ pressureColor WRITE setPressureColor NOTIFY toolOptionsChanged)
    Q_PROPERTY(int pressureMinSize READ pressureMinSize WRITE setPressureMinSize NOTIFY toolOptionsChanged)
    Q_PROPERTY(int pressureDither READ pressureDither WRITE setPressureDither NOTIFY toolOptionsChanged)
    Q_PROPERTY(QVariantList pressureCurve READ pressureCurve WRITE setPressureCurve NOTIFY toolOptionsChanged)
    Q_PROPERTY(bool tiltAngle READ tiltAngle WRITE setTiltAngle NOTIFY toolOptionsChanged)
    bool pressureSize() const { return m_pressureSize; }
    void setPressureSize(bool on);
    bool pressureOpacity() const { return m_pressureOpacity; }
    void setPressureOpacity(bool on);
    bool pressureColor() const { return m_pressureColor; }
    void setPressureColor(bool on);
    int pressureMinSize() const { return m_pressureMinSize; }
    void setPressureMinSize(int percent);
    int pressureDither() const { return m_pressureDither; }
    void setPressureDither(int pattern);
    QVariantList pressureCurve() const;
    void setPressureCurve(const QVariantList& curve);
    bool tiltAngle() const { return m_tiltAngle; }
    void setTiltAngle(bool on);
    // The pointer driving the next beginStroke / moveStroke: a pen (with its pressure 0..1 and Qt's tilts
    // in degrees) or not (a mouse: full pressure, no tilt). Set by the canvas before each of them.
    void setPenState(bool pen, double pressure, double xTilt = 0, double yTilt = 0);
    // Flip and scale are kept apart from the brush's own pixels (m_customSource), so scaling down and
    // back up loses nothing. The scale is a step: 0 = x1, 2 = x3, -1 = half (every 2nd pixel), -2 = a third.
    Q_PROPERTY(int customBrushScale READ customBrushScale WRITE setCustomBrushScale NOTIFY customBrushChanged)
    Q_PROPERTY(QString customBrushScaleText READ customBrushScaleText NOTIFY customBrushChanged)
    Q_PROPERTY(bool customBrushEdited READ customBrushEdited NOTIFY customBrushChanged)
    Q_INVOKABLE void flipCustomBrush(bool horizontal);
    Q_INVOKABLE void resetCustomBrush(); // back to x1, not flipped
    int customBrushScale() const { return m_customScale; }
    void setCustomBrushScale(int step); // stays put when the result would be too big or empty
    QString customBrushScaleText() const;
    bool customBrushEdited() const { return m_customScale != 0 || m_customFlipH || m_customFlipV; }
    static constexpr int kMaxCustomBrush = 256;
    bool hasCustomBrush() const { return !m_customBrush.isNull(); }
    QImage customBrushImage() const;
    QSize customBrushSize() const { return {m_customBrush.width(), m_customBrush.height()}; }
    bool brushColors() const { return m_brushColors; }
    void setBrushColors(bool on);
    bool dither() const { return m_dither; }
    void setDither(bool on);
    int ditherPattern() const { return m_ditherPattern; }
    void setDitherPattern(int kind);
    int ditherDensity() const { return m_ditherDensity; }
    void setDitherDensity(int density);
    bool ditherOtherColor() const { return m_ditherOtherColor; }
    void setDitherOtherColor(bool on);
    // A pattern as a size x size RGBA tile (white = painted), for the UI.
    Q_INVOKABLE QImage ditherPreview(int kind, int density, int size) const;
    bool symmetryHorizontal() const { return m_symmetryH; }
    void setSymmetryHorizontal(bool on);
    bool symmetryVertical() const { return m_symmetryV; }
    void setSymmetryVertical(bool on);
    bool symmetryRising() const { return m_symmetryRising; }
    int symmetryRadial() const { return m_symmetryRadial; }
    void setSymmetryRadial(int copies);
    bool symmetryRadialMirror() const { return m_symmetryRadialMirror; }
    void setSymmetryRadialMirror(bool on);
    bool symmetryRadialTurn() const { return m_symmetryRadialTurn; }
    void setSymmetryRadialTurn(bool on);
    void setSymmetryRising(bool on);
    bool symmetryFalling() const { return m_symmetryFalling; }
    void setSymmetryFalling(bool on);
    QPointF symmetryCenter() const { return {m_symmetryCenter2.x / 2.0, m_symmetryCenter2.y / 2.0}; }
    void setSymmetryCenter(QPointF center);
    Q_INVOKABLE void centerSymmetry();
    // The brush for the canvas's brush preview: its footprint (white =
    // covered), or with previewsBrushColors() the custom brush's own colors.
    QImage brushImage() const;
    bool previewsBrushColors() const;
    // Changes whenever brushImage() would (for the canvas's texture upload).
    qint64 brushKey() const;
    // Width and height of the current brush.
    QSize brushExtent() const;
    // Rectangle and ellipse tools draw filled shapes.
    bool fillShapes() const { return m_fillShapes; }
    void setFillShapes(bool on);
    // Fill tool: only the connected area, or every matching pixel of the
    // cel; tolerance 0..255 per channel (0: the exact color).
    bool fillContiguous() const { return m_fillContiguous; }
    void setFillContiguous(bool on);
    int fillTolerance() const { return m_fillTolerance; }
    void setFillTolerance(int tolerance);
    // Magic wand: the same choices, for what it selects from the current cel.
    bool wandContiguous() const { return m_wandContiguous; }
    void setWandContiguous(bool on);
    bool sampleMerged() const { return m_sampleMerged; }
    int tileMode() const { return m_tileMode; }
    int paletteLoadMode() const { return m_paletteLoadMode; }
    void setPaletteLoadMode(int mode);
    QStringList fonts() const;
    int textFont() const { return m_textFont; }
    void setTextFont(int index);
    int textLetterSpacing() const { return m_textLetterSpacing; }
    void setTextLetterSpacing(int pixels);
    int textLineSpacing() const { return m_textLineSpacing; }
    void setTextLineSpacing(int pixels);
    int gradientShape() const { return m_gradientShape; }
    void setGradientShape(int shape);
    bool gradientRamp() const { return m_gradientRamp; }
    void setGradientRamp(bool on);
    QVariantList gradientLine() const { return m_gradientLine; }
    int gradientDither() const { return m_gradientDither; }
    void setGradientDither(int pattern);
    int gradientRepeat() const { return m_gradientRepeat; }
    void setGradientRepeat(int repeat);
    bool gradientCustom() const { return m_gradientCustom; }
    void setGradientCustom(bool on);
    QVariantList gradientCustomStops() const;
    void setGradientCustomStops(const QVariantList& stops);
    QString gradientStopsText() const;
    void setGradientStopsText(const QString& text);
    bool gradientEditable() const { return m_gradientEdit.valid; }
    // Moves the ends of the gradient just drawn (canvas pixels) and draws it again.
    Q_INVOKABLE void editGradient(double ax, double ay, double bx, double by);
    // Leaves it as it is (Enter, a click elsewhere); cancelGradient takes it back.
    Q_INVOKABLE void finishGradient();
    Q_INVOKABLE void cancelGradient();
    int textAlign() const { return m_textAlign; }
    void setTextAlign(int align);
    int textCursor() const { return m_textCursor; }
    void setTextCursor(int index);
    QRect textCaret() const;
    bool textActive() const { return m_text.active; }
    QString text() const { return m_text.text; }
    Q_INVOKABLE void setText(const QString& text);
    void setTileMode(int mode);
    bool tileX() const { return m_tileMode & 1; }
    bool tileY() const { return m_tileMode & 2; }
    // `pixel` moved into the sprite on the tiled axes (as shown on a copy).
    Q_INVOKABLE QPoint wrapPixel(QPoint pixel) const;
    void setSampleMerged(bool on);
    int wandTolerance() const { return m_wandTolerance; }
    void setWandTolerance(int tolerance);
    // True for tools whose size is the brush size.
    Q_INVOKABLE bool usesBrush(Tool tool) const;

    // The selection's bounds in document pixels (empty = none). The selection
    // itself is a mask of any shape: drawing and filling stay inside it, the
    // move tool moves its pixels. The select, lasso and magic wand tools add
    // with Shift, subtract with Alt and intersect with both, held at the
    // start of a drag (or the click).
    // The document's grid: x / y offset and cell width / height.
    QRect grid() const;
    bool snapToGrid() const { return m_snapToGrid; }
    void setSnapToGrid(bool on);
    // `r` grown to whole grid cells (as it is when snapping is off).
    Q_INVOKABLE QRect snapRect(const QRect& r) const;
    // `value` (an x, or a y when `vertical`) on the nearest grid line.
    Q_INVOKABLE int snapEdge(int value, bool vertical) const;
    // `offset` of something at `at` changed so it lands on a cell corner.
    Q_INVOKABLE QPoint snapMove(QPoint at, QPoint offset) const;
    QVariantList slices() const;
    int currentTilesetIndex() const { return m_current && m_current->kind == core::LayerKind::Tilemap ? int(m_current->tilesetIndex) : -1; }
    bool tilemapLayer() const { return currentTileset() != nullptr; }
    bool editTiles() const { return m_editTiles; }
    void setEditTiles(bool on);
    QString tilesetName() const;
    QSize tileSize() const;
    int tileBaseIndex() const;
    int tileCount() const;
    int currentTile() const { return m_currentTile; }
    void setCurrentTile(int index);
    int tilesetRevision() const { return m_tilesetRevision; }
    // Tile `index` of the current layer's tileset, in RGBA (null if none).
    Q_INVOKABLE QImage tileImage(int index) const;
    // A new empty tile at the end of the tileset, made current. Undoable.
    Q_INVOKABLE int addTile();
    // Managing the current tileset's tiles (the empty tile 0 stays put). Each is
    // one undo step that also remaps every tilemap cel using the tileset:
    // a deleted tile's cells become empty, the others follow their tile.
    // Opens tile `index` as a small sprite in its own tab with the tiled view on
    // both axes, to draw it and see it repeat. Every change goes back to the
    // tileset (one undo step each in the source document, which stays in its
    // tab); the tab never asks to be saved.
    Q_INVOKABLE bool editTileInTab(int index);
    Q_INVOKABLE int duplicateTile(int index);   // a copy at the end, picked; its index
    Q_INVOKABLE bool deleteTile(int index);
    Q_INVOKABLE bool moveTile(int from, int to); // `to` is the index it ends up at
    Q_INVOKABLE int removeUnusedTiles();         // how many went
    Q_INVOKABLE int mergeDuplicateTiles();       // how many merged away
    // How many tiles those two would take away, and how many cells show tile `index`.
    Q_INVOKABLE int unusedTileCount() const;
    Q_INVOKABLE int duplicateTileCount() const;
    Q_INVOKABLE int tileUses(int index) const;
    // How many cells show each tile, by tile index (one pass over the maps; tile 0, the empty one, counts too).
    Q_INVOKABLE QVariantList tileUseCounts() const;
    int tileFlips() const { return int(m_tileFlips >> 29); }
    void setTileFlips(int flips);
    bool tilesActive() const { return m_editTiles && tilemapLayer(); }
    int tileEditMode() const { return m_tileEditMode; }
    void setTileEditMode(int mode);
    // The tilemap grid cell under `pixel`, in canvas pixels (empty if none).
    Q_INVOKABLE QRect tileCellAt(QPoint pixel) const;
    // Tile mode, besides the pencil, eraser and eyedropper: the fill tool fills
    // the cells joined to the clicked one that show the same tile; the line and
    // rectangle tools place the tile along / in a dragged cell rectangle (the
    // fill-shapes option makes the rectangle solid). Dragging the eyedropper
    // over several cells picks them as a stamp the pencil places as a block
    // (picking one tile in the panel, or a click, clears it).
    // tileStamp: its size in cells (0 x 0: none); tilePickRect: the cells the
    // eyedropper is dragging over, in canvas pixels (empty: none);
    // tileCursorImage: what the pencil would place, flips included.
    Q_PROPERTY(QSize tileStamp READ tileStamp NOTIFY tileCursorChanged)
    Q_PROPERTY(QRect tilePickRect READ tilePickRect NOTIFY tileCursorChanged)
    QSize tileStamp() const { return {m_stampW, m_stampH}; }
    QRect tilePickRect() const { return m_tilePickRect; }
    Q_INVOKABLE QImage tileCursorImage() const;
    Q_INVOKABLE void clearTileStamp();
    // A block of tiles from the tileset panel as the stamp: `ids` row by row, `columns` to a row
    // (cells past the end are empty). One tile just becomes the current tile. The stamp then has
    // the orientation the flip buttons are showing.
    Q_INVOKABLE void setTileStamp(int columns, const QVariantList& ids);
    // The tiles the stamp uses (each once, no empty cells), for the tileset panel to mark them.
    Q_INVOKABLE QVariantList tileStampIds() const;
    QVariantList allSlices() const;
    int currentSlice() const { return m_currentSlice; }
    void setCurrentSlice(int index);
    // Slice edits, each undoable; drags of one slice merge into one step.
    // New slices are named "Slice N" in the default color and become current.
    Q_INVOKABLE int addSlice(const QRect& bounds);
    // The key in effect at the current frame.
    Q_INVOKABLE void setSliceBounds(int index, const QRect& bounds);
    Q_INVOKABLE void setSliceName(int index, const QString& name);
    Q_INVOKABLE void setSliceColor(int index, const QColor& color);
    Q_INVOKABLE void removeSlice(int index);
    bool sliceKeys() const { return m_sliceKeys; }
    void setSliceKeys(bool on);
    // 9-slice: on gives every key a center (the middle third) if it has
    // none; the center is relative to the bounds. Pivot: on puts it in the
    // middle of every key; relative to the bounds' origin.
    Q_INVOKABLE void setSliceNineSlice(int index, bool on);
    Q_INVOKABLE void setSliceCenter(int index, const QRect& center);
    Q_INVOKABLE void setSliceHasPivot(int index, bool on);
    Q_INVOKABLE void setSlicePivot(int index, const QPoint& pivot);
    // Takes away the current frame's own key (not the first one), so the
    // key before it applies again.
    Q_INVOKABLE void removeSliceKey(int index);
    Q_INVOKABLE void setGrid(int x, int y, int width, int height);
    QRect selection() const;
    Q_INVOKABLE bool selectionContains(QPoint pixel) const { return m_doc->selection.contains(pixel.x(), pixel.y()); }
    bool hasSelection() const { return !m_doc->selection.isEmpty(); }
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselect();
    bool lassoPolygon() const { return m_lassoPolygon; }
    void setLassoPolygon(bool on);
    bool polygonActive() const { return m_polygon.active; }
    QList<QPointF> lassoPath() const;
    QString dragInfo() const { return m_stroke.active ? m_stroke.info : QString(); }
    // Polygon lasso: the corners so far become the selection (with the mode
    // the first click picked), as one undo step; with fewer than two corners
    // it's a click (deselects without Shift / Alt). Cancel drops them;
    // removing the only corner cancels.
    Q_INVOKABLE void closePolygon();
    Q_INVOKABLE void cancelPolygon();
    Q_INVOKABLE void removePolygonCorner();
    // Selects the document grid's cell under `pixel` (the select tool's
    // double-click while the grid is shown), cut to the canvas; Shift adds,
    // Alt subtracts, both intersect. Right after a click that deselected (the
    // double-click's first click) it replaces that undo step.
    Q_INVOKABLE bool selectGridCell(QPoint pixel, Qt::KeyboardModifiers modifiers);
    // Everything that isn't selected (all of the canvas without a selection).
    Q_INVOKABLE void invertSelection();
    // The opaque pixels of a layer's cel in the current frame (a group: of
    // the layers in it).
    Q_INVOKABLE void selectLayerPixels(int index);
    // The layer's cel on the current frame as it sits on the canvas, fitted
    // into `maxSide` pixels (nearest); null for groups, tilemaps and empty cels.
    Q_INVOKABLE QImage layerImage(int index, int maxSide) const;
    // Clears the selected pixels of the current cel (or drops a paste); with
    // nothing selected, every pixel of the cel.
    Q_INVOKABLE void deleteSelection();
    // Arrow keys: moves the selected pixels (they float until Enter or the
    // next edit) or, with `outlineOnly`, just the selection. False if there's
    // nothing to move.
    Q_INVOKABLE bool nudge(int dx, int dy, bool outlineOnly);
    bool nudge(int dx, int dy) { return nudge(dx, dy, false); } // (C++ only: QML always passes all of them)

    // --- clipboard: pixels --------------------------------------------------
    // Copy takes the selected pixels of the current cel (the whole canvas
    // area without a selection) onto the system clipboard as an image. Paste
    // takes any image from it and floats it above the current layer at the
    // spot it was copied from (centered if it came from elsewhere); the move
    // tool, or dragging inside the selection, moves it. It lands as one undo
    // step on commitFloating(), or when anything else edits the document.
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    // The same, centered on `center` (the pixel under the pointer; kept on the
    // canvas). Paste goes here when the pointer is over the canvas, so
    // paste() is Paste in Place.
    Q_INVOKABLE void pasteAt(QPoint center);
    // What every visible layer shows (inside the selection), not just the
    // current layer.
    Q_INVOKABLE void copyMerged();
    // The clipboard's image onto a new layer above the current one ("Pasted"),
    // floating like a paste; or as a new sprite of its size (a new tab).
    // False when the clipboard has no image.
    Q_INVOKABLE bool pasteAsNewLayer();
    Q_INVOKABLE bool pasteAsNewLayerAt(QPoint center);
    Q_INVOKABLE bool pasteAsNewSprite();
    static int maxImageSize() { return core::kMaxImageSize; }
    bool canPaste() const;
    // The size of the image on the system clipboard (invalid when there is none, or it is
    // over kMaxImageSize square): the New Sprite dialog starts at it.
    Q_INVOKABLE QSize clipboardImageSize() const;

    // --- floating pixels: paste and transform ---------------------------------
    // A paste, or pixels lifted by startTransform() (the selection, or all of
    // the cel's pixels, also any outside the canvas), float above the layer until they land. They
    // can be moved, scaled to a box, rotated, flipped and turned by 90
    // degrees; everything is applied in one undo step on commitFloating(),
    // or whenever anything else edits the document. cancelFloating() drops
    // a paste or puts lifted pixels back.
    Q_PROPERTY(QRect transformBox READ transformBox NOTIFY floatingChanged)
    Q_PROPERTY(double transformAngle READ transformAngle NOTIFY floatingChanged)
    Q_PROPERTY(QSize transformSourceSize READ transformSourceSize NOTIFY floatingChanged)
    // The point of the box that stays put when it's rotated or given a new
    // size by number: 0..8, row by row (0 top-left, 4 the center, 8
    // bottom-right). A tool option, remembered.
    Q_PROPERTY(int transformPivot READ transformPivot WRITE setTransformPivot NOTIFY toolOptionsChanged)
    Q_INVOKABLE void startTransform();

    // --- effects: outline, replace color, hue / saturation -------------------
    // Shown live on the current cel (inside the selection, if any) while
    // their dialog is open, then applied as one undo step or put back. Any
    // other edit meanwhile (undo, a palette change, ...) puts it back first
    // and ends it. previewEffect options:
    //   {kind: "outline", color, inside, square}
    //   {kind: "replace", from, to, tolerance}
    //   {kind: "adjust", hue, saturation, lightness}
    //   {kind: "blur", radius, gaussian, keepShape}
    //   {kind: "cleanup", orphans, pinholes, strays, doubles}
    //   {kind: "curves", master, red, green, blue: flat [x0, y0, x1, y1, ...] control points, 0..255}
    //   {kind: "posterize", levels (2..16 per channel)}
    //   {kind: "shadow", color (its alpha is the opacity), dx, dy}
    //   {kind: "convolve", kernel: [int] (size x size, row by row), size: 3 | 5, divisor (0: auto), bias}
    // beginEffect is false when the current cel can't be edited (or is empty).
    Q_PROPERTY(bool effectActive READ effectActive NOTIFY effectChanged)
    Q_INVOKABLE bool beginEffect();
    Q_INVOKABLE void previewEffect(const QVariantMap& options);
    // Applies the last preview to `scope`'s cels too (one undo step):
    // "cel" (just the current one), "range" (the timeline's selected cels or
    // frames), "tag" (the current layer across the current frame's tag),
    // "layer" (the current layer, every frame), "sprite" (every cel).
    // Hidden and locked layers are left alone; linked cels change once.
    Q_INVOKABLE void applyEffect(const QString& label, const QString& scope);
    void applyEffect(const QString& label) { applyEffect(label, QStringLiteral("cel")); }
    Q_INVOKABLE void cancelEffect();
    // Which cels the preview shows (the same scopes as applyEffect): the
    // other cels of the current frame in it change on the canvas too.
    // The 256 outputs of a curve given as flat control points, for drawing it.
    Q_INVOKABLE QVariantList curveTable(const QVariantList& flatPoints) const;
    Q_INVOKABLE void setEffectScope(const QString& scope);
    bool effectActive() const { return m_effect.active || m_modePreview; }
    // The current cel's color at `pixel` as it was before the preview
    // (transparent outside it), for picking an effect's colors on the canvas.
    Q_INVOKABLE QColor effectColorAt(QPoint pixel) const;
    Q_INVOKABLE void commitFloating();
    Q_INVOKABLE void cancelFloating();
    bool floating() const { return m_float.active; }
    bool floatingLifted() const { return m_float.active && m_float.lifted; }
    // The box the pixels are scaled into (before rotation), and the angle in
    // degrees, clockwise, around the box center.
    QRect transformBox() const;
    double transformAngle() const { return m_float.angle; }
    QSize transformSourceSize() const;
    Q_INVOKABLE void setTransformBox(const QRect& box);
    Q_INVOKABLE void setTransformAngle(double degrees);
    int transformPivot() const { return m_transformPivot; }
    void setTransformPivot(int pivot);
    // A new size by number, keeping the pivot in place.
    Q_INVOKABLE void resizeTransform(int width, int height);
    // On the floating pixels, or (lifted and placed at once) on the selection
    // or the current cel.
    Q_INVOKABLE void flipHorizontal();
    Q_INVOKABLE void flipVertical();
    Q_INVOKABLE void rotate90(bool clockwise);

    // --- clipboard: timeline ------------------------------------------------
    // Frames (frame range: whole frames with durations) or a block of cels
    // (cel range, or the current cel). Pasted frames go after the current
    // frame; a pasted block's top-left lands on the current cel.
    Q_INVOKABLE void copyTimeline();
    Q_INVOKABLE void cutTimeline();
    Q_INVOKABLE void pasteTimeline();
    // Cels only paste into a document of the same color mode.
    bool canPasteTimeline() const
    {
        return !m_timelineClip.rows.empty() && m_timelineClip.mode == m_doc->colorMode();
    }

    QColor primaryColor() const { return m_primary; }
    void setPrimaryColor(const QColor& c);
    QColor secondaryColor() const { return m_secondary; }
    void setSecondaryColor(const QColor& c);

    QVariantList palette() const;
    // Palette edits, each one undo step. In indexed sprites the pixels are
    // remapped so the picture stays the same (a removed entry's pixels take
    // the nearest remaining color); the transparent index can't be removed.
    // Returns the index of the color (existing one if already there), or -1.
    Q_INVOKABLE int addPaletteColor(const QColor& color);
    Q_INVOKABLE bool removePaletteColor(int index);
    Q_INVOKABLE bool movePaletteColor(int from, int to);
    Q_INVOKABLE bool setPaletteColor(int index, const QColor& color);
    // Replaces the palette from a file (.gpl, .hex, .aseprite / .ase, an image;
    // a path or a qrc ":/" path), as one undo step. Indexed pixels keep their
    // indices (a palette swap); indices past the new palette's end take its
    // nearest color. Emits errorOccurred on failure.
    Q_INVOKABLE bool loadPalette(const QString& path);
    // The same from a list of colors (a palette from Lospec). False if none.
    Q_INVOKABLE bool setPaletteColors(const QVariantList& colors);
    // The current frame of an indexed sprite as it would look after loading
    // `colors` (per paletteLoadMode), scaled to fit `maxSize` pixels, as a
    // PNG data URL for an Image. Empty for other color modes. Nothing changes.
    Q_INVOKABLE QString palettePreview(const QVariantList& colors, int maxSize);
    // Writes the palette as .gpl (or .hex, .pal, .act by extension) named `name`.
    Q_INVOKABLE bool savePalette(const QString& path, const QString& name);
    // Palette tools, each one undo step. They work on the shade range (or
    // the whole palette), skipping an indexed sprite's transparent entry,
    // which stays where it is; the range stays selected. False if there's
    // nothing to do.
    // Ramp: the entries between the first and the last become an even blend
    // between those two (OKLab). Needs 3 entries. Indexed pixels keep their
    // indices, so they take the new colors.
    // With `hueArc` the hue turns the short way around at constant chroma
    // instead of blending straight (a hue-shifted ramp).
    Q_INVOKABLE bool rampPalette(bool hueArc);
    // Needs a range of 2 or more: `count` new colors, a ramp between the
    // range's first and last colors, go right before the last one (between
    // two neighbors, exactly between them). Later entries move up and indexed
    // pixels follow; the range grows to include them. False without a range,
    // or when an indexed palette would pass 256.
    Q_INVOKABLE bool insertRamp(int count, bool hueArc);
    // Sort by "hue" (grays first), "saturation" or "lightness"; or reverse.
    // Indexed pixels follow their colors (the picture stays).
    Q_INVOKABLE bool sortPalette(const QString& key);
    Q_INVOKABLE bool reversePalette();
    // Merges the entries down to `count` colors by median cut, weighted by
    // how many pixels use each; indexed pixels take the nearest kept color.
    // RGBA and grayscale pixels stay as they are.
    Q_INVOKABLE bool reducePalette(int count);
    // The colors the palette tools would make, for a look before applying:
    // what Reduce keeps, and the colors Insert adds (nothing when it can't).
    Q_INVOKABLE QVariantList previewReduce(int count);
    Q_INVOKABLE QVariantList previewInsert(int count, bool hueArc);
    // Reduce on the canvas for an indexed sprite (the current frame): shown as
    // an effect preview, nothing in the document changes; the sprite as it is
    // when Reduce wouldn't change anything. Ends with cancelColorModePreview.
    Q_INVOKABLE void previewReducePixels(int count);
    // Removes entries that no pixel uses (`unused`) and/or merges entries of
    // the same color into the first (`duplicates`). Indexed pixels follow;
    // false if nothing would go. An indexed sprite's transparent entry is
    // never touched.
    Q_INVOKABLE bool removeUnusedColors(bool unused, bool duplicates);
    // How many entries those two would remove.
    Q_INVOKABLE int removableColors(bool unused, bool duplicates) const;
    // How many entries the palette tools work on.
    Q_INVOKABLE int paletteToolEntries() const { return int(paletteToolIndices().size()); }
    // Layer rows, bottom to top: each group comes right after its children
    // (so, read top first, a group's row is above its children), children of
    // collapsed groups skipped. Maps also carry opacity (percent) and blend.
    QVariantList layers() const;
    int currentLayer() const;
    void setCurrentLayer(int index);
    bool canRemoveLayer() const;

    int frameCount() const { return m_doc->frameCount(); }
    int currentFrame() const { return m_currentFrame; }
    void setCurrentFrame(int frame);
    int frameDuration() const { return m_doc->frame(m_currentFrame).durationMs; }
    void setFrameDuration(int ms);
    bool playing() const { return m_playing; }
    // Plays the innermost tag under the current frame (its direction and
    // repeat count; stops after the last repeat), or loops all frames.
    void setPlaying(bool playing);
    int playingTag() const { return m_playing ? m_playingTag : -1; }

    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }

    // Display name ("Untitled", or the PNG it came from, until saved) and
    // location of the open .aseprite file.
    QString fileName() const;
    QUrl fileUrl() const;
    QString filePath() const { return m_filePath; } // absolute, empty until saved
    bool modified() const { return m_modified; }

    Q_INVOKABLE void newDocument(int width, int height);
    // New sprite. Options (all optional):
    //   width, height: 1..maxImageSize (default 64)
    //   colorMode: "rgba" | "grayscale" | "indexed"
    //   background: "transparent" | "white" | "black" (opaque: a Background layer)
    //   frames: 1..9999
    Q_INVOKABLE void newSprite(const QVariantMap& options);

    // Documents. New and opened sprites get their own tab (an untouched
    // startup sprite is replaced instead); opening a file that's already
    // open switches to it.
    QVariantList documents() const;
    int currentDocument() const { return m_currentDoc; }
    void setCurrentDocument(int index);
    // Closes without asking (the window confirms unsaved changes first).
    // Closing the last one leaves a new untitled sprite.
    Q_INVOKABLE void closeDocument(int index);
    // Index of the open document saved at `path`, or -1.
    Q_INVOKABLE int documentForPath(const QString& path) const;
    // Moves tab `from` to `to` (both indices before the move); the current
    // document stays current.
    Q_INVOKABLE void moveDocument(int from, int to);
    // Shows the file in Finder / Explorer (its folder elsewhere).
    Q_INVOKABLE static void revealFile(const QString& path);
    Q_INVOKABLE static bool fileExists(const QString& path);
    Q_INVOKABLE bool isDocumentModified(int index) const;
    // Stays the same for a document while it's open (e.g. to keep its view).
    QString documentId() const { return m_recoveryId; }

    // Crash recovery. Documents with unsaved changes are copied to a recovery
    // folder autosaveDelay seconds after the last change (written in the
    // background); saving or closing one removes its copy, quitting removes
    // them all. After a crash the next start finds them:
    // {name, path, saved} for each, then restore (as unsaved tabs) or discard.
    int autosaveDelay() const { return m_autosaveDelay; }
    void setAutosaveDelay(int seconds);
    QSize newGridSize() const { return m_newGridSize; }
    void setNewGridSize(QSize size);
    // Back to their defaults: the grid for new sprites (Preferences > Canvas)
    // and the crash recovery delay (Preferences > Files).
    Q_INVOKABLE void resetNewGridSize();
    Q_INVOKABLE void resetAutosaveDelay();
    int undoMemory() const { return m_undoMemory; }
    void setUndoMemory(int megabytes);
    Q_INVOKABLE void resetUndoMemory();
    Q_INVOKABLE QVariantList recoverableDocuments();
    Q_INVOKABLE int restoreRecovered();
    Q_INVOKABLE void discardRecovered();
    // Tests: another recovery folder, autosave at once and wait for the
    // writes, and leave the files behind on destruction as if crashed.
    void setRecoveryRoot(const QString& root) { m_recoveryRoot = root; }
    void autosaveNow();
    void waitForAutosave();
    void setKeepRecoveryOnExit(bool keep) { m_keepRecovery = keep; }
    // Scales the whole sprite (every cel) to width x height, nearest-neighbor.
    // `method`: "nearest", "smooth" (Scale2x-based, no new colors) or "bilinear".
    Q_INVOKABLE void resizeSprite(int width, int height, const QString& method);
    void resizeSprite(int width, int height) { resizeSprite(width, height, QStringLiteral("nearest")); }
    // New canvas in current canvas coordinates (x / y < 0 grows left / up);
    // cels are cut to it, background layers fill new area with the secondary color.
    Q_INVOKABLE void resizeCanvas(const QRect& bounds);
    // The current frame at `width` x `height` (nearest neighbor), as a PNG data
    // URL for a thumbnail; the longer side is cut to `maxSide` pixels.
    Q_INVOKABLE QString spriteSizePreview(int width, int height, int maxSide, const QString& method);
    QString spriteSizePreview(int width, int height, int maxSide) { return spriteSizePreview(width, height, maxSide, QStringLiteral("nearest")); }
    // Crops the canvas to what any frame shows (visible layers). False when
    // there is nothing to show or the canvas already fits it.
    Q_INVOKABLE bool trimCanvas();
    // Converts the sprite to `mode` ("rgba" | "grayscale" | "indexed"), one
    // undo step. To indexed, `palette` "current" maps pixels to the nearest
    // entry of the current palette, "sprite" makes a palette of the sprite's
    // colors (reduced to 255 + transparent if there are more). False if
    // nothing changed (same mode, or tilemap layers).
    // `dither` (going to indexed): "none", "ordered" (Bayer 4 x 4), "bayer2",
    // "bayer8", "blue" (noise), "floyd", "atkinson", "sierra", "stucki" or "riemersma".
    // `ditherOptions`: {strength 0..100 (100), serpentine, oklab}, see core::DitherOptions.
    // A live look at a conversion (the dialog's options) on the canvas, for the
    // current frame only; nothing in the document changes. It ends with
    // cancelColorModePreview or anything that cancels an effect preview, and
    // counts as `effectActive` meanwhile (the canvas doesn't draw).
    // previewColorMode returns the palette the sprite would have (indexed).
    Q_INVOKABLE bool beginColorModePreview();
    // `palette`: "sprite" (`colors` of its own, besides transparent), "current"
    // or "given" (the `given` colors, a transparent entry put first).
    Q_INVOKABLE QVariantList previewColorMode(const QString& mode, const QString& palette, const QString& dither,
                                              int colors, const QVariantList& given, bool keepAlpha,
                                              const QVariantMap& ditherOptions);
    Q_INVOKABLE void cancelColorModePreview();
    Q_INVOKABLE bool setColorMode(const QString& mode, const QString& palette, const QString& dither, int colors,
                                  const QVariantList& given, bool keepAlpha, const QVariantMap& ditherOptions);
    bool setColorMode(const QString& mode, const QString& palette, const QString& dither, int colors,
                      const QVariantList& given, bool keepAlpha)
    {
        return setColorMode(mode, palette, dither, colors, given, keepAlpha, {});
    }
    // False with tilemap layers (their tilesets can't be converted yet).
    Q_INVOKABLE bool canChangeColorMode() const;
    // Distinct visible colors in the sprite, counting up to `limit` + 1.
    Q_INVOKABLE int spriteColorCount(int limit) const;
    // Return false and emit errorOccurred on failure.
    Q_INVOKABLE bool openFile(const QUrl& url);
    // .aseprite/.ase, or an image (PNG, JPEG, BMP, TGA, GIF with all its
    // frames) as a new, unsaved document.
    Q_INVOKABLE bool openPath(const QString& path);
    // Image files as the frames of a new, unsaved document.
    Q_INVOKABLE bool openImages(const QStringList& paths);
    // Import a sprite sheet: the current frame of the current document cut into tiles that become the
    // frames of a new, unsaved document. options: x, y, width, height (the first tile), padX, padY
    // (space between tiles), columns, rows, byColumns (frames run down the columns first), skipEmpty
    // (fully transparent tiles are left out). The sheet's own pixels are untouched.
    Q_INVOKABLE bool importSpriteSheet(const QVariantMap& options);
    // The columns and rows (as width and height) that fit the current frame for options' x, y, width,
    // height, padX and padY; with `partial`, a tile cut off by the edge counts.
    Q_INVOKABLE QSize spriteSheetFit(const QVariantMap& options, bool partial) const;
    // The animations in a folder and its subfolders: [{name, count (frames), files}], by name.
    Q_INVOKABLE QVariantList scanImageFolder(const QString& folder) const;
    // One new, unsaved document of those animations (clips: [{name, files}], as scanImageFolder gives
    // them), a tag for each. `folder` names the document.
    Q_INVOKABLE bool openImageClips(const QVariantList& clips, const QString& folder);
    // The numbered image sequence `path` belongs to (just itself if none).
    Q_INVOKABLE QStringList imageSequence(const QString& path) const;
    // True for the image types openPath and importLayer read.
    Q_INVOKABLE bool isImageFile(const QString& path) const;
    // Adds an image as a new layer above the current one, on the current frame, and starts
    // transforming it: scaled down to fit the canvas if it's bigger (Enter keeps that, Esc keeps it
    // as is). A GIF with several frames makes a layer with a cel for each, from the current frame
    // on (the sprite gets more frames, with the GIF's delays, if it has too few): one undo step,
    // and no transform.
    Q_INVOKABLE bool importLayer(const QUrl& url);
    Q_INVOKABLE static QString toLocalPath(const QUrl& url);
    // Saves to the current file; false if it has none yet (use saveFileAs).
    Q_INVOKABLE bool saveFile();
    Q_INVOKABLE bool saveFileAs(const QUrl& url);

    // Tags as {name, from, to}, for picking export ranges.
    QVariantList tags() const;
    // Exports without touching the document. Options:
    //   format: "png" | "gif" | "sheet"
    //   range:  "all" | "current" | tag index (int)
    //   scale:  1..64
    //   layout: "horizontal" | "vertical" | "grid" | "tags" (sheet only;
    //           "tags" = one row per tag and ignores range)
    Q_INVOKABLE bool exportFile(const QUrl& url, const QVariantMap& options);
    // Frame (cell) count, sheet rows and output size in pixels
    // {frames, rows, width, height} for the
    // given options, so the UI can preview them.
    Q_INVOKABLE QVariantMap exportInfo(const QVariantMap& options) const;
    // Prepares a preview of what exporting with `options` gives, at scale 1
    // (the preview fits its box): a sheet is one image; PNG and GIF are the
    // range's frames, a GIF's as the GIF will look (alpha on / off, at most
    // 256 colors, delays in hundredths of a second), decoded from the
    // encoded file. Returns {durations: [ms per frame]}. GIFs over
    // kGifPreviewPixels show the plain frames instead.
    Q_INVOKABLE QVariantMap prepareExportPreview(const QVariantMap& options);
    // Frame `index` of the last prepared preview (null if out of range).
    Q_INVOKABLE QImage exportPreviewFrame(int index) const;
    // Frame `frame` as shown (every visible layer, RGBA), for the preview
    // panel; null if out of range. The current frame is the live composite.
    Q_INVOKABLE QImage frameImage(int frame) const;
    Q_INVOKABLE int frameDurationAt(int frame) const;
    // The preview panel's own playback, with the tag rules of `setPlaying` (the
    // tag under `frame`: its direction and repeat count; or every frame, looping)
    // but without moving the editor's frame. `start` returns `frame` (or -1 if
    // there is nothing to play); `next` the frame after the last one it gave,
    // or -1 once the last repeat has played.
    Q_INVOKABLE int startPreviewPlayback(int frame);
    Q_INVOKABLE int nextPreviewFrame();
    Q_INVOKABLE void stopPreviewPlayback();
    // The tag `frame` is in (the shortest when several hold it, as playback picks), or -1.
    Q_INVOKABLE int tagIndexAt(int frame) const;
    static constexpr qint64 kGifPreviewPixels = 64 * 1024 * 1024;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void swapColors();

    Q_INVOKABLE void addLayer();
    // Live layers: the current layer is one when it has a particle recipe (its cels are rendered from
    // it and can't be painted on). The emitter is where the particles are born, in canvas pixels.
    Q_PROPERTY(bool liveLayer READ liveLayer NOTIFY liveChanged)
    Q_PROPERTY(QPointF liveEmitter READ liveEmitter WRITE setLiveEmitter NOTIFY liveChanged)
    Q_INVOKABLE void addParticleLayer();
    // A new live layer with the preset's settings (an id from particlePresetList; empty: a plain one).
    Q_INVOKABLE void addParticleLayerFrom(const QString& presetId);
    // The presets that can be applied: {id, name, description} each.
    Q_INVOKABLE QVariantList particlePresetList() const;
    // The user's own presets (saved from a layer): {id, name} each; changes with presetsChanged.
    Q_PROPERTY(QVariantList userPresets READ userPresets NOTIFY presetsChanged)
    QVariantList userPresets() const;
    // Saves the current live layer's settings as a preset called `name` (the same name replaces it): the
    // emitter and the fields' places become relative to it, sizes and speeds go back to a sprite 64
    // pixels tall, and it is kept in the app data folder. False when there is nothing to save.
    Q_INVOKABLE bool saveParticlePreset(const QString& name);
    Q_INVOKABLE void deleteUserPreset(const QString& id);
    // Keeps the user presets in `folder` instead of the app data folder (tests).
    void setPresetFolder(const QString& folder);
    // Quick effects (Effects menu): ready-made live layers. Adds one above the current layer, with the
    // preset scaled to the sprite, and extends the sprite to the frames its effect needs: one undo step.
    Q_INVOKABLE void addQuickEffect(const QString& presetId);
    // What the Quick Effects menu offers: the presets (`group` false), then the groups of them.
    Q_INVOKABLE QVariantList quickEffectList() const;
    // Replaces the current live layer's settings with the preset's (empty id: the built-in plain
    // emitter), keeping where its emitter is and its seed. One undo step.
    Q_INVOKABLE void applyParticlePreset(const QString& presetId);
    // What the reset dots compare with: the values of the preset the layer started from, or the
    // built-in ones; the same names as particleSettings.
    Q_PROPERTY(QVariantMap particleReference READ particleReference NOTIFY liveChanged)
    QVariantMap particleReference() const;
    // Adds empty frames at the end until the sprite has `total` (one undo step), keeping the current
    // frame: for an effect that needs more frames than there are.
    Q_INVOKABLE void addFramesUpTo(int total);
    bool liveLayer() const { return m_current && m_current->isLive(); }
    QPointF liveEmitter() const;
    void setLiveEmitter(QPointF position);
    // The current live layer's recipe (a default one when it isn't live).
    core::ParticleRecipe particleRecipe() const;
    // Replaces the recipe; one undo step (with `continuous`, edits of the same kind in a row are one).
    void setParticleRecipe(const core::ParticleRecipe& recipe, const char* label, bool continuous = false);
    // The recipe as plain values for the settings panel: numbers by name (enums and flags as ints,
    // `colors` as a list of colors). Empty when the current layer isn't live.
    Q_PROPERTY(QVariantMap particleSettings READ particleSettings NOTIFY liveChanged)
    QVariantMap particleSettings() const;
    // Sets one numeric field of the recipe by its name (see particleSettings), clamped and kept
    // consistent (end >= start, min <= max). `continuous` merges drags into one undo step.
    Q_INVOKABLE void setParticleField(const QString& key, double value, bool continuous);
    // A custom easing for the size or the opacity over a life ("sizeCurve", "opacityCurve"; also in
    // particleSettings as [x1, y1, x2, y2]); its easing becomes Custom (4).
    Q_INVOKABLE void setParticleCurve(const QString& key, double x1, double y1, double x2, double y2, bool continuous);
    // The built-in value of every field, the same names as particleSettings.
    Q_PROPERTY(QVariantMap particleDefaults READ particleDefaults CONSTANT)
    QVariantMap particleDefaults() const;
    // Puts the named fields (or "colors") back to their built-in values: one undo step.
    Q_INVOKABLE void resetParticleFields(const QStringList& keys);
    Q_INVOKABLE void setParticleColors(const QVariantList& colors);
    // The frames the effect emits on (dragged on the timeline); a burst only has `start`.
    Q_INVOKABLE void setParticleRange(int start, int end, bool continuous);
    Q_INVOKABLE void reseedParticles();
    // Which recipe the panel edits: 0 the particles, 1 their sub-particles (only when the layer has some:
    // see `subTrigger` in particleSettings). Every setting call (setParticleField, setParticleColors,
    // resetParticleFields, reseedParticles) and particleSettings / particleReference follow it.
    Q_PROPERTY(int particleContext READ particleContext WRITE setParticleContext NOTIFY liveChanged)
    int particleContext() const;
    void setParticleContext(int context);
    // Forces from places on the canvas (see core::ParticleField); `index` counts in particleSettings'
    // `fields`. Keys of a field: x, y, strength, radius, falloff, angle, turbulence, scale, mode, bounciness, friction, stack.
    // `type`: 0 attractor / repeller, 1 vortex, 2 wind, 3 floor, 4 collider. Returns the new field's index, or -1.
    // A collider's corners are `points` ({x, y} in canvas pixels) in particleSettings' fields.
    Q_INVOKABLE int addParticleField(int type);
    Q_INVOKABLE void removeParticleField(int index);
    Q_INVOKABLE void setParticleFieldValue(int index, const QString& key, double value, bool continuous);
    Q_INVOKABLE void setParticleFieldPosition(int index, double x, double y, bool continuous);
    // A collider's corners, in canvas pixels. Inserting one (before `at`) and then moving it is one undo
    // step; a collider keeps at least two, and at most 32.
    Q_INVOKABLE void setColliderPoint(int index, int point, double x, double y, bool continuous);
    Q_INVOKABLE int insertColliderPoint(int index, int at, double x, double y);
    Q_INVOKABLE void removeColliderPoint(int index, int point);
    // The particles take the shape of the current custom brush (its opacity, kept in the recipe, so the
    // layer stays as it is when the brush changes); false without a live layer or a custom brush, or
    // for one over 128 pixels.
    Q_INVOKABLE bool useCustomBrushAsParticle();
    // Bake Layer: the live layer becomes an ordinary layer holding the pictures it showed (one undo
    // step). Bake to New Layer keeps the live layer and adds an ordinary copy above it, which becomes
    // the current layer.
    // The tools that work on a live layer: the eyedropper and hand (they don't change pixels) and Move
    // (it drags the emitter). The others can't be picked while one is current; the tool it replaces
    // comes back when another layer is.
    Q_INVOKABLE static bool toolUsableOnLive(int tool);
    // The tools of a Smear group (current): Pose (its handles), Move (the whole group: its art on every
    // frame and its pivot), the eyedropper and the hand. The strip swaps to them, as for a live layer.
    Q_INVOKABLE static bool toolUsableOnSmear(int tool);
    // The tools of a live layer. The source tool drags out a line, rectangle or oval (`liveSourceKind`
    // 1, 2, 3) and puts the emitter at its middle; the field tool places a field (`liveFieldKind` 0
    // attractor, 1 vortex, 2 wind, 3 floor, 4 collider) where it is clicked and moves it while the button
    // is down; a collider is a roof where it is clicked, or a straight line along the drag.
    Q_PROPERTY(int liveSourceKind READ liveSourceKind WRITE setLiveSourceKind NOTIFY toolOptionsChanged)
    Q_PROPERTY(int liveFieldKind READ liveFieldKind WRITE setLiveFieldKind NOTIFY toolOptionsChanged)
    int liveSourceKind() const { return m_liveSourceKind; }
    void setLiveSourceKind(int kind);
    int liveFieldKind() const { return m_liveFieldKind; }
    void setLiveFieldKind(int kind);
    // While the animation plays the canvas only pans and zooms (as in Aseprite), so a click never stops
    // it. The exception is a live layer's emitter: with it current, the Move tool (drags the emitter) and
    // the eyedropper (colors for the particles) keep working during playback.
    bool lockedToPan() const;
    void syncLiveTool();
    void beginSourceDraw(QPoint pixel, Qt::KeyboardModifiers modifiers);
    void dragSourceTo(QPoint pixel);
    void placeFieldAt(QPoint pixel);
    int addFieldImpl(int type, bool placed, double x, double y);
    // A Smear group shows as drawn while the current layer is inside it (so a brush lands where the art
    // is drawn); true when that changed for any group.
    bool syncSmearAsDrawn();
    void refreshSmearState();
    QVariantMap m_smearState;
    struct UserSmearPreset {
        QString id;
        QString name;
        core::SmearRecipe relative;
    };
    mutable std::vector<UserSmearPreset> m_userSmearPresets;
    mutable std::map<QString, std::vector<QImage>> m_smearPreviews; // by preset id, from smearPresetPreview
    mutable bool m_userSmearPresetsLoaded = false;
    void loadUserSmearPresets() const;
    core::Image smearArt() const;
    core::Rect smearArtBox() const;
    std::optional<core::SmearRecipe> smearPresetFor(const QString& id, core::Rect art, int start) const;
    std::optional<core::SmearPose> m_smearPoseClipboard;
    // Whether any visible Smear group shows posed (then a change anywhere can show anywhere).
    bool anyPosedSmear() const;
    core::ParticleRecipe referenceRecipe() const;
    const core::ParticlePreset* findAnyPreset(const std::string& id) const;
    void loadUserPresets() const;
    QString presetFolder() const;
    mutable std::vector<core::ParticlePreset> m_userPresets;
    mutable bool m_userPresetsLoaded = false;
    QString m_presetFolder;
    core::ParticleRecipe newParticleRecipe(const QString& presetId) const;
    // Layers a file said were particle effects but whose pixels were changed elsewhere (see
    // Layer::staleRecipe): {index, name} of each, `index` in the order of the document's layers.
    // Resolving one either renders the effect again or keeps the pixels as an ordinary layer.
    Q_INVOKABLE QVariantList staleLiveLayers() const;
    Q_INVOKABLE void resolveStaleLive(int index, bool renderAgain);
    Q_INVOKABLE void bakeLayer();
    Q_INVOKABLE void bakeToNewLayer();
    Q_INVOKABLE void removeLayer();
    Q_INVOKABLE void setLayerVisible(int index, bool visible);
    Q_INVOKABLE void setLayerName(int index, const QString& name);
    Q_INVOKABLE void setLayerCollapsed(int index, bool collapsed);
    Q_INVOKABLE void setLayerLocked(int index, bool locked);
    // Makes the topmost visible image layer with a pixel at `pixel` (canvas
    // coordinates, current frame) current; false if there's none (Cmd / Ctrl
    // + click on the canvas). Layers inside collapsed groups aren't picked.
    Q_INVOKABLE bool selectLayerAt(QPoint pixel);
    // Its name ("" when there's none), for the Cmd / Ctrl hover label.
    Q_INVOKABLE QString layerNameAt(QPoint pixel) const;
    // Painting, fills and effects only change pixels the layer already has
    // (keeping their alpha). Saved in .aseprite files (see Layer::kAlphaLockFlag).
    Q_INVOKABLE void setLayerAlphaLock(int index, bool on);
    Q_INVOKABLE void setLayerOpacity(int index, int percent);
    Q_INVOKABLE void setLayerBlend(int index, int mode);
    Q_INVOKABLE QStringList blendModes() const;
    // Above the current layer, in its group.
    Q_INVOKABLE void addGroup();
    Q_INVOKABLE void duplicateLayer();
    // Moves layer `index` next to layer `target`: zone 0 above it, 1 below
    // it (as shown, top first), 2 into it (a group; on top of its children).
    // False if it can't go there (e.g. a group into itself).
    Q_INVOKABLE bool moveLayer(int index, int target, int zone);
    // The layers the timeline's cel range covers (a layer inside a covered
    // group counts with it), else the current one; bottom to top.
    std::vector<core::Layer*> selectedLayers() const;
    // The picked layers, in list order; they count only while the current layer is one of them.
    std::vector<core::Layer*> m_picks;
    std::unique_ptr<core::Document> m_layerClipDoc; // RGBA, only to hold the copied layers' pixels
    std::vector<std::unique_ptr<core::Layer>> m_layerClip;
    bool importFrameLayer(std::vector<core::Image> images, const std::vector<int>& durations, const std::string& name);
    bool picksActive() const;
    bool moveLayers(const std::vector<core::Layer*>& layers, core::Layer* onto, int zone);
    void removeLayers(std::vector<core::Layer*> layers);
    // A new layer with `layer`'s properties (name, visibility, opacity, ...).
    std::unique_ptr<core::Layer> layerLike(const core::Layer& layer, core::LayerKind kind) const;
    // Swaps the current layer for `layer` (and the tilesets for `tilesets`), one step.
    // The tileset `options` picks, added to `tilesets` when it's a new one.
    uint32_t tilesetFor(const QVariantMap& options, std::vector<core::Tileset>& tilesets,
                        const std::string& defaultName) const;
    void replaceCurrentLayer(std::unique_ptr<core::Layer> layer, std::vector<core::Tileset> tilesets, const char* label);
    // Puts the selected layers into a new group where the topmost one was,
    // or a group's children back where the group was (the group goes). One
    // undo step each; false if there's nothing to do.
    Q_INVOKABLE bool groupLayers();
    // Layer > Smear: the selected layers go into a new Smear group (a live group that poses and smears
    // them, core/Smear) where the topmost one was; its pivot starts in the middle of their art and it has
    // no keys yet (the art shows as drawn). One undo step; false if there's nothing to do.
    Q_INVOKABLE bool smearLayers();
    // The Smear group the current layer is or is in (nullptr: none), its recipe, and changing it (one
    // undo step; `continuous` merges a drag).
    core::Layer* smearGroup() const;
    // Posing the current Smear group on the canvas (SmearHandles.qml). `smearState`: `current` (a Smear
    // group is the current layer), `frame`, `key` (the frame has a key), `keys` (their frames), `pose`
    // ({x, y, rotation, scaleX, scaleY, skewX, skewY} on this frame), `pivot` (as drawn) and `pivotPosed`,
    // `box` (the art as drawn on this frame, canvas pixels), `corners` (the box posed, clockwise from top
    // left) and `matrix` ([a, b, c, d]: the pose's turn, skew and scale).
    Q_PROPERTY(QVariantMap smearState READ smearState NOTIFY smearChanged)
    QVariantMap smearState() const { return m_smearState; }
    // Sets the pose of the current frame: its key, made (from the tweened pose) if there is none.
    Q_INVOKABLE void setSmearPose(const QVariantMap& pose, bool continuous);
    // The pivot, as drawn (it is where the art turns about at every key). Every key's move is adjusted so
    // the posed art stays where it is on every key.
    Q_INVOKABLE void setSmearPivot(double x, double y, bool continuous);
    Q_INVOKABLE void removeSmearKey(int frame);
    // 0 linear, 1 in, 2 out, 3 in-out: how the motion goes from the key on `frame` to the next.
    Q_INVOKABLE void setSmearKeyEase(int frame, int ease);
    // 4 above (Custom) is this curve (core::EasingCurve): the key on `frame` eases by it.
    Q_INVOKABLE void setSmearKeyCurve(int frame, double x1, double y1, double x2, double y2, bool continuous);
    // A key dragged along the timeline to `to` (copied with `copy`); a key already there is replaced. One
    // undo step; the current frame follows it.
    Q_INVOKABLE void moveSmearKey(int from, int to, bool copy);
    // A pose kept to paste on another key (or frame): not saved, shared by the documents.
    Q_INVOKABLE void copySmearPose(int frame);
    Q_INVOKABLE void pasteSmearPose(int frame);
    Q_PROPERTY(bool canPasteSmearPose READ canPasteSmearPose NOTIFY smearChanged)
    bool canPasteSmearPose() const { return m_smearPoseClipboard.has_value(); }
    // The smear's own settings (also in smearState, by the same names): trail (0..1), behind (0 / 1),
    // style (0 streak, 1 flat), color, taper (0..1), fade (0..1), fadePattern (0..4), outline (0 / 1),
    // outlineColor (colors as QColor), smoothPath, facePath (0 / 1). One undo step; `continuous` merges a drag.
    Q_INVOKABLE void setSmearSetting(const QString& key, const QVariant& value, bool continuous);
    // Back to the built-in values, one undo step: the smear settings by name (as in setSmearSetting), and
    // the pose's (x, y, rotation, scaleX, scaleY, skewX, skewY) on the current frame's key (made if none).
    Q_INVOKABLE void resetSmearSettings(const QStringList& keys);
    // Ready-made motions ({id, name, description}, see core::smearPresets), and applying one to the
    // current Smear group: its keys from the current frame, pivot and smear settings, made for the art
    // (the sprite gets the frames it needs), one undo step.
    Q_INVOKABLE QVariantList smearPresetList() const;
    Q_PROPERTY(QVariantList smearPresets READ smearPresetList NOTIFY presetsChanged)
    Q_INVOKABLE void applySmearPreset(const QString& id);
    // A preset on the current group's art, for the preset gallery: renders every frame (cropped to where
    // it moves) and keeps them, returning {count, still: the frame that tells most at rest, where the
    // smear covers most}; empty when there is nothing to show. `smearPresetFrame` hands one out (a QImage
    // straight from here: kept in a QML array it could be shown only once).
    Q_INVOKABLE QVariantMap smearPresetPreview(const QString& id) const;
    Q_INVOKABLE QImage smearPresetFrame(const QString& id, int frame) const;
    // Presets of your own, saved as files in the preset folder (ids "user:..."); the same name replaces.
    // Made independent of the art (core::relativeSmear), so they fit any sprite.
    Q_INVOKABLE bool saveSmearPreset(const QString& name);
    Q_INVOKABLE void deleteSmearPreset(const QString& id);
    Q_INVOKABLE bool renameSmearPreset(const QString& id, const QString& name);
    std::optional<core::SmearRecipe> smearRecipe() const;
    void setSmearRecipe(const core::SmearRecipe& recipe, const char* label, bool continuous = false);
    // Tilemaps, with tiles the size of the document grid's cells:
    // a new tilemap layer (with a new, empty tileset) above the current one;
    // the current image layer as a tilemap (its cels cut on the grid,
    // identical tiles shared; a background can't be one); the current
    // tilemap layer as a normal one (its tiles drawn as pixels). One undo
    // step each; false if it can't.
    // Tilesets a tilemap layer can use: [{name, tileWidth, tileHeight,
    // baseIndex, tiles (not counting the empty one)}], by tileset index.
    Q_INVOKABLE QVariantList tilesetList() const;
    // options: tileset (an index from tilesetList, or -1 / missing for a new
    // one) and, for a new one, name, tileWidth, tileHeight (default: the
    // grid's) and baseIndex (default 1). No default argument: QML would
    // call the generated no-argument overload and drop the options.
    Q_INVOKABLE bool addTilemapLayer(const QVariantMap& options);
    Q_INVOKABLE bool convertToTilemap(const QVariantMap& options);
    // Import a tileset image: loadTilesetSheet reads the picture (false, and
    // an error, if it can't), tilesetSheet / tilesetSheetCells show it with
    // the tiles outlined for a tile size, margin and spacing, and
    // importTileset (options: name, tileWidth, tileHeight, margin, spacing,
    // skipEmpty, merge: equal tiles once, layout: the tiles on the new layer
    // as the picture shows them, tileset: add them to one the sprite has
    // instead) makes a new tileset of its tiles, row by row, on a new
    // tilemap layer, in Tiles mode. The tile count is tilesetSheetCount.
    Q_INVOKABLE bool loadTilesetSheet(const QUrl& url);
    Q_INVOKABLE QString tilesetSheetName() const { return m_sheetName; }
    Q_INVOKABLE QImage tilesetSheet() const;
    Q_INVOKABLE QSize tilesetSheetSize() const { return {m_sheet.width(), m_sheet.height()}; }
    Q_INVOKABLE QVariantList tilesetSheetCells(int tileWidth, int tileHeight, int margin, int spacing) const;
    Q_INVOKABLE QSize tilesetSheetGrid(int tileWidth, int tileHeight, int margin, int spacing) const;
    // A guess at the tile size of the loaded picture (no margin or spacing); invalid if nothing stands out.
    Q_INVOKABLE QSize detectTilesetSize() const;
    Q_INVOKABLE bool importTileset(const QVariantMap& options);
    // Export the current tilemap layer's tileset for an engine (PNG of its
    // tiles without the empty one; options: columns, margin, spacing, tsx,
    // tmx, csv; `url` names the files, the extension is replaced). The info
    // map is {width, height, columns, rows, tiles, layers} (layers: tilemap
    // layers of that tileset with a cel on this frame, which tmx / csv write).
    Q_INVOKABLE bool exportTileset(const QUrl& url, const QVariantMap& options);
    Q_INVOKABLE QVariantMap tilesetExportInfo(int columns, int margin, int spacing) const;
    Q_INVOKABLE QImage tilesetExportImage(int columns, int margin, int spacing) const;
    Q_INVOKABLE bool convertToImageLayer();
    Q_INVOKABLE bool ungroupLayer();

    // Layer rows (same order as layers()) and the document, read-only, for
    // the timeline model.
    const std::vector<core::Layer*>& layerRows() const { return m_layerList; }
    const core::Document& document() const { return *m_doc; }

    Q_INVOKABLE void addFrame();
    // These act on the frames of the timeline range, or the current frame.
    Q_INVOKABLE void duplicateFrame();
    Q_INVOKABLE void removeFrame();
    Q_INVOKABLE void reverseFrames();
    // Duration of every frame of the range (or the current frame), 1..65535 ms.
    Q_INVOKABLE void setFramesDuration(int ms);

    // --- timeline range -----------------------------------------------------
    // x / width: frames; y / height: layer indices (into layers()). For a
    // frame range y / height cover every layer.
    RangeType rangeType() const { return m_rangeType; }
    QRect range() const;
    Q_INVOKABLE void selectFrames(int from, int to);
    Q_INVOKABLE void selectCels(int layerA, int frameA, int layerB, int frameB);
    // A block of layers taken in the layer column (Shift-click): the current frame of those layers,
    // marked so Delete removes the layers where a cel range of the grid only clears its cels.
    Q_INVOKABLE void selectLayerBlock(int layerA, int layerB);
    Q_INVOKABLE void clearRange();
    // Layers that aren't next to each other (Cmd / Ctrl-click in the layers list or the timeline's
    // layer column): the layer is added to the selection (the picks, a Shift-click block or just the
    // current layer), or taken out of it. Group, Delete and drag-to-move act on all of them.
    // layerPicks: how many are picked (0 when the selection isn't a set of picks).
    Q_PROPERTY(int layerPicks READ layerPickCount NOTIFY rangeChanged)
    Q_INVOKABLE void togglePick(int layerIndex);
    // Layers between documents: copyLayers keeps the selected layers (a group with its children) as
    // they are, and pasteLayers puts them above the current layer, as one undo step, in this document or
    // another one (colors go to its color mode; frames the target doesn't have are left out). Tilemap
    // layers can't be copied (their tilesets belong to one document): false, and an error, when nothing
    // could be. Kept for the whole run, whatever document is open.
    Q_PROPERTY(bool canPasteLayers READ canPasteLayers NOTIFY layerClipboardChanged)
    Q_INVOKABLE bool copyLayers();
    Q_INVOKABLE bool pasteLayers();
    bool canPasteLayers() const { return !m_layerClip.empty(); }
    Q_INVOKABLE bool isPicked(int layerIndex) const;
    int layerPickCount() const { return picksActive() ? int(m_picks.size()) : 0; }
    Q_INVOKABLE bool inRange(int layerIndex, int frame) const;
    // Frame range: moves the frames by `frameDelta`. Cel range: moves (or
    // copies) the cels by layers and frames; false if they wouldn't fit.
    Q_INVOKABLE bool moveRange(int layerDelta, int frameDelta, bool copy);
    bool moveRange(int layerDelta, int frameDelta) { return moveRange(layerDelta, frameDelta, false); }
    // Cels of the range (or the current cel).
    Q_INVOKABLE void clearCels();
    // Every cel of the current layer, on all frames (not when it's locked).
    Q_INVOKABLE void clearLayer();
    Q_INVOKABLE void linkCels();
    Q_INVOKABLE void unlinkCels();

    // Tags as {name, from, to, color, direction, repeat}.
    Q_INVOKABLE void addTag(); // over the range's frames (or the current frame)
    Q_INVOKABLE void setTag(int index, const QVariantMap& values);
    Q_INVOKABLE void removeTag(int index);
    // Deletes the tag and every frame it covers (one undo step; refused when that would leave no frame).
    Q_INVOKABLE void removeTagAndFrames(int index);
    Q_INVOKABLE void nextFrame();
    Q_INVOKABLE void previousFrame();

    // Input from the canvas, in document pixel coordinates. Shift constrains
    // shapes (45-degree lines, squares, circles) and selections; Alt draws
    // them from the center.
    void beginStroke(QPoint pixel, bool secondary, Qt::KeyboardModifiers modifiers = {});
    void moveStroke(QPoint pixel, Qt::KeyboardModifiers modifiers = {});
    void setStrokeModifiers(Qt::KeyboardModifiers modifiers);
    void endStroke();
    bool isStroking() const { return m_stroke.active; }
    // The stroke paints along the pointer's path (pencil, eraser, blur, jumble, placing tiles), so the
    // canvas may stabilize it.
    bool isFreehandStroke() const { return m_stroke.active && (m_stroke.kind == Stroke::Freehand || m_stroke.kind == Stroke::Tiles); }

    // Composited current frame (RGBA8888, borrowed; copy before keeping).
    QImage composite() const;
    // Region of the composite changed since the last call.
    QRect takeDirty();

    // Onion skin: neighbouring frames shown faintly behind the current one
    // (not while playing). Before / after: 0..10 frames; opacity in percent
    // for the nearest frame; tint: red before, blue after.
    bool onionSkin() const { return m_onionEnabled; }
    void setOnionSkin(bool on);
    int onionBefore() const { return m_onionOptions.before; }
    void setOnionBefore(int frames);
    int onionAfter() const { return m_onionOptions.after; }
    void setOnionAfter(int frames);
    int onionOpacity() const { return (m_onionOptions.opacity * 100 + 127) / 255; }
    void setOnionOpacity(int percent);
    bool onionTint() const { return m_onionOptions.tint; }
    void setOnionTint(bool on);
    bool onionFront() const { return m_onionFront; }
    void setOnionFront(bool on);
    bool onionLoop() const { return m_onionLoop; }
    void setOnionLoop(bool on);
    // The onion skin image (RGBA8888, borrowed) and its changed region.
    QImage onion() const;
    QRect takeOnionDirty();
    // The selection as a canvas-sized RGBA image (white = selected) for the
    // marching ants, and what changed in it since the last call.
    QImage selectionImage() const;
    QRect takeSelectionDirty();

    // Tool previews drawn over the canvas, never part of the document
    // (RGBA8888, borrowed), and the region changed since the last call.
    QImage overlay() const;
    QRect takeOverlayDirty();
    // Pencil/eraser with Shift held: preview the straight line that a
    // Shift-click at `hover` would draw from the last painted pixel.
    void setLinePreview(QPoint hover, bool active);
    void setHoverInside(bool inside);
    // Last pixel a pencil/eraser stroke painted (document coordinates).
    std::optional<QPoint> lastPainted() const;

signals:
    void documentChanged();
    void documentsChanged();
    // A file was opened or saved (for recent files; not on tab switches).
    void fileUsed(const QString& path);
    void canvasSizeChanged(); // also on undo / redo of a size change
    void colorModeChanged();  // also on undo / redo and tab switches
    void toolChanged();
    void brushSizeChanged();
    void toolOptionsChanged();
    void onionChanged();
    void preferencesChanged();
    void paletteLoadModeChanged();
    void tileModeChanged();
    void textChanged();
    void textStarted(); // a click with the text tool: focus the text field (it has `text`)
    void effectChanged();
    void selectionChanged();
    void floatingChanged();
    void clipboardChanged();
    void colorsChanged();
    void paletteChanged();
    void shadeRangeChanged();
    void customBrushChanged();
    void symmetryCenterChanged();
    void polygonChanged();
    void lassoPathChanged();
    void dragInfoChanged();
    void layersChanged();
    // Cels were added or removed (layers or frames may be unchanged).
    void celsChanged();
    void currentLayerChanged();
    void framesChanged();
    void tagsChanged();
    void gridChanged();
    void snapToGridChanged();
    void slicesChanged();
    void tilesetChanged();
    void tileCursorChanged();
    void layerClipboardChanged();
    void gradientLineChanged();
    void rangeChanged();
    void currentFrameChanged();
    void frameDurationChanged();
    void playingChanged();
    void historyChanged();
    void canvasChanged();
    void fileChanged();
    void liveChanged();
    void smearChanged();
    void presetsChanged();
    // The field tool put a new field here (an index into particleSettings' `fields`).
    void liveFieldPlaced(int index);
    // A file was opened with layers that need a decision (staleLiveLayers).
    void staleLiveFound();
    void modifiedChanged();
    void errorOccurred(const QString& message);

private:
    // A brush as RGBA (already turned by the angle) and the variants a symmetry copy stamps: oriented by
    // a core::Mirror element and, for radial copies that turn the brush, turned by that copy's share of a
    // full turn. Variants are made when first asked for (in the document's format or in RGBA).
    struct BrushSet {
        core::Image rgba;
        const core::Document* doc = nullptr; // set: the variants are converted to the document's format
        int copies = 0;                      // radial copies (a turn is 360 / copies)
        mutable std::map<int, core::ImageBrush> variants;
        int width() const { return rgba.width(); }
        int height() const { return rgba.height(); }
        const core::ImageBrush& at(int element, int turn = 0) const;
    };
    // One symmetry copy of a stamp: where, how the brush is oriented, and (radial) which share of a turn.
    struct Copy {
        core::Point p;
        int element = 0;
        int turn = 0; // the brush's share of a radial turn (0 when the brush isn't turned)
        int step = 0; // the copy's share of a radial turn, whether or not the brush turns
    };
    // A piece of the selection that moves with the Move tool when symmetry is on: its pixels, and how a
    // drag moves it (the copy's element, then its radial turn).
    struct MovePart {
        core::Mask mask;
        core::Image block; // over mask.bounds(), transparent outside the mask
        int element = 0;
        int step = 0;
    };
    core::Point partOffset(const MovePart& part, core::Point offset) const;
    void splitMoveByPress(core::Point press);
    void updateMoveParts(core::Point offset);

    // The Move tool with several things selected (layers, cels, frames): the cels and live layers it moves.
    struct LiveStart {
        core::Layer* layer;
        core::ParticleRecipe before;
    };
    bool multiMoveActive() const;
    // Several layers, cels or frames are selected (and nothing is floating or selected as pixels).
    bool multiSelectionActive() const;
    void beginMoveMany(QPoint pixel);
    // What a move or flip of the selection covers.
    struct MoveGroup {
        std::vector<std::pair<core::CelRef, core::Point>> cels; // each cel and where it started
        std::vector<core::Layer*> celLayers;                    // the layer of each
        std::vector<LiveStart> live;
    };
    void gatherMoveTargets(MoveGroup& group) const;
    void shiftGroup(const std::vector<std::pair<core::CelRef, core::Point>>& cels, std::vector<LiveStart>& live, core::Point offset);
    // Cmd+T on a selection of several cels: they are all lifted into one transform (the same box,
    // angle and pivot for every one), previewed on the current frame, and land each in its own cel.
    bool liftMulti(const char* label);
    void updateMultiLive();
    void flipMany(bool horizontal);

    struct Stroke {
        // Pick: Alt held with the pencil, eraser or fill picks colors instead.
        // Tiles: the pencil / eraser in tile mode, setting tilemap cells.
        std::vector<std::pair<core::CelRef, core::Point>> manyCels; // MoveMany: each cel and where it started
        std::vector<LiveStart> manyLive;                           // and each live layer's recipe at the start
        enum Kind { Freehand, Shape, Select, MoveSelection, MoveCel, MoveFloat, Pick, Tiles, TileShape, TilePick, MoveCells, MoveEmitter, Source, Field, MoveMany, MoveSmear };

        bool active = false;
        Kind kind = Freehand;
        Qt::KeyboardModifiers modifiers;
        std::shared_ptr<core::Image> image;
        core::Image before;
        core::Point origin; // cel position in the document
        core::Point start;  // image coordinates (document for Select/Move*)
        core::Point last;
        QString info;       // dragInfo: the size being dragged out
        core::Rect bounds;  // everything changed so far, image coordinates
        core::Rect drawn;   // what the last shape/move step changed
        bool useStamp = false; // Tiles: place the picked block, not one tile
        uint32_t value = 0;
        std::optional<BrushSet> brushes; // custom or turned brush in the document's format
        bool brushOwnColors = false;
        // Pen dynamics (see setPenState): the size and angle change per stamp (`dynamic`; the stamp's
        // brush set, if any, in `stampBrushes`, made once per size and angle in `dynamicBrushes`), and
        // the ink takes its opacity / color from the pressure, pixel by pixel (`penInk`: `inkMask`
        // marks one stamp's pixels, `strength` the firmest press each pixel got, for opacity).
        bool pen = false;
        bool customStamp = false; // the custom brush (its size doesn't follow the pressure)
        bool dynamic = false;
        bool dynamicSize = false;
        bool dynamicAngle = false;
        bool penInk = false;
        bool inkOpacity = false;
        bool inkColor = false;
        bool inkErases = false;
        core::Rgba inkHigh, inkLow; // firm and light colors
        double pressure = 1;        // at the last stamp
        const BrushSet* stampBrushes = nullptr;
        std::map<int64_t, BrushSet> dynamicBrushes;
        core::Image inkMask;
        core::Image strength;
        bool inkOwn = false;   // a custom brush in its own colors: they are the firm color, pixel by pixel
        core::Image inkColors; // one stamp's own colors (the document's format; transparent elsewhere)
        bool lassoTail = false; // the last lasso point follows the pointer (Alt: a straight segment)
        // Dithering: the pixels the stroke covered (Indexed8, 1 = covered;
        // null = not dithering) and what the pattern's skipped ones take
        // (none: they stay as they were).
        core::Image covered;
        std::optional<uint32_t> ditherOff;
        std::vector<uint32_t> ramp; // shading ink: the ramp (empty = plain painting)
        bool softens = false;       // the blur / jumble brushes (`jumble`: which), from `before`
        bool jumble = false;
        uint32_t seed = 0;
        // Pixel mode on a tilemap layer: `image` is the map laid out as
        // pixels; each step's cells go back into the tileset (tileEditMode).
        bool tiles = false;
        std::shared_ptr<core::Image> map; // the tilemap cel's image
        core::Image mapBefore;
        size_t tilesetId = 0;
        core::Tileset tilesetBefore; // as it was (its tile images untouched since)
        size_t tilesAtStart = 0;
        std::unordered_map<int, uint32_t> stackTiles; // Stack: cell -> its new tile
        int shadeStep = 1;
        QString label;
        core::PixelPerfectFilter filter;
        // Pixel-perfect bookkeeping: each new pixel of the stroke is a step;
        // a corner is only put back if the stroke first painted it at the
        // step being undone, never when an earlier part of the stroke did.
        std::unordered_map<uint64_t, int> firstPainted; // pixel -> step
        // With symmetry: how many kept points (and their mirror images) cover each pixel, so removing a
        // corner doesn't put back a pixel another part of the stroke still paints.
        std::unordered_map<uint64_t, int> owners;
        std::vector<MovePart> parts; // MoveSelection with symmetry: each piece, moved its own way
        int step = 0;
        int lastStep = 0; // step of the filter's last kept pixel
        std::optional<core::Point> lastPushed;
        bool moved = false;
        bool createdCel = false; // this stroke created the current cel
        bool fromSelectTool = false; // a move begun inside the selection with the select tool
        // MoveCells: the selected cells (cell coordinates in the map and their values), whether they
        // are copied, and how far, in cells, they have moved.
        std::vector<std::pair<core::Point, uint32_t>> cells;
        bool copyCells = false;
        core::Point cellOffset;
        // Move tool
        core::Image floating;
        core::Mask selectionStart; // also the base an add / subtract drag starts from
        enum SelectMode { Replace, Add, Subtract, Intersect } selectMode = Replace;
        std::vector<core::Point> lasso; // the lasso's outline so far (document coordinates)
        Qt::KeyboardModifiers modeModifiers; // held at the start: they pick the mode, not constraints
        core::CelRef cel;
        core::Point celStart; // also the paste's start position
    };

    core::Layer* layerAt(int index) const;
    const core::Layer* topLayerAt(QPoint pixel) const; // see selectLayerAt
    void rebuildLayerList();
    void setDocument(std::unique_ptr<core::Document> doc, const QString& path, const QString& untitledName = {});
    core::Layer* insertLayerAboveCurrent(std::unique_ptr<core::Layer> layer);
    bool saveTo(const QString& path);
    void updateModified();
    uint32_t encodeColor(const QColor& color) const;
    uint32_t transparentValue() const;
    // The shading ink's ramp as pixel values: the shade range (clamped to
    // the palette) or the whole palette, without transparent entries.
    std::vector<uint32_t> shadeRamp() const;
    // Image of the current cel, ready for drawing. Creates the cel on an empty
    // frame unless `create` is false; `created` reports whether it did.
    std::shared_ptr<core::Image> editableImage(core::Point& origin, bool* created = nullptr, bool create = true);
    // Records a pixel edit of `r` (image coordinates) as one undo step,
    // together with the creation of the cel if this edit created it.
    void pushPixelEdit(const std::shared_ptr<core::Image>& image, core::Rect r, const core::Image& before,
                       const std::string& label, bool createdCel);
    void dropCreatedCel();
    void clickDeselect();
    core::Image currentCelRgba(core::Rect area) const;
    void plotSegment(core::Point from, core::Point to);
    void drawShape();
    void updateSelectionStroke();
    void beginSelectStroke(QPoint pixel, Qt::KeyboardModifiers modifiers);
    void magicWand(QPoint pixel, Qt::KeyboardModifiers modifiers);
    static Stroke::SelectMode selectModeFor(Qt::KeyboardModifiers modifiers);
    static const char* selectLabel(Stroke::SelectMode mode);
    core::Mask combinedSelection(const core::Mask& base, const core::Mask& shape, Stroke::SelectMode mode) const;
    void beginMove(QPoint pixel);
    void updateMove();
    void restoreOutsideSelection(core::Rect area);
    // With the current layer alpha locked, keeps the stroke off its empty pixels.
    void keepAlphaInStroke(core::Rect area);
    // Tiled view: moves what a stroke step painted outside the canvas (on a
    // tiled axis) to the other side; returns `area` plus where it went.
    core::Rect wrapStroke(core::Rect area);
    core::DitherPattern coreDither() const;
    // The dither pass over `area` of the stroke's image (image coordinates).
    void ditherStroke(core::Rect area);
    core::Rect selectionInStrokeImage() const;
    void setSelection(core::Mask mask);
    bool isTransparent(uint32_t value) const;
    bool canDrawOnCurrentLayer() const;
    void refreshOverlay();
    void renderOnion(core::Rect area);
    void pickColor(QPoint pixel, bool secondary);
    bool m_pickSecondary = false; // what an Alt pick stroke sets
    void fill(QPoint pixel, bool secondary);
    void execute(std::unique_ptr<core::Command> command, bool merge = false);
    void setDurations(int first, int last, int ms);
    bool continuing(const QString& key);
    // Changes a layer's own properties as one undo step; `continuous` edits
    // of the same kind on the same layer merge (one scrub = one step).
    void setLayerProps(core::Layer* layer, const std::function<void(core::LayerProps&)>& change, const char* label,
                       bool continuous = false);
    void structureChanged();
    void invalidate(core::Rect rect);
    // While playing, each frame's composite is kept and shown again on the next loop. Any other redraw
    // (an edit, a structure change) drops them, so a stale frame is never shown.
    void showPlaybackFrame();
    void clearFrameCache();
    std::vector<std::optional<core::Image>> m_frameCache;
    size_t m_frameCacheBytes = 0;
    mutable std::vector<QImage> m_previewCache; // the Preview panel's frames (not the current one), by frame
    mutable size_t m_previewCacheBytes = 0;
    static constexpr size_t kFrameCacheLimit = 192u * 1024 * 1024;
    void invalidateAll();
    void advancePlayback();

    std::unique_ptr<core::Document> m_doc;
    core::UndoStack m_undo;
    core::Image m_composite;
    core::Rect m_dirty;

    Tool m_tool = Pencil;
    int m_particleContext = 0;
    int m_liveSourceKind = 2;
    int m_liveFieldKind = 0;
    int m_toolBeforeLive = -1; // the tool a live layer replaced with Move, to come back to (-1: none)
    int m_brushSize = 1;
    BrushShape m_brushShape = SquareBrush;
    int m_brushAngle = 0;
    bool m_brushBlend = false;
    bool m_pressureSize = true;
    bool m_pressureOpacity = false;
    bool m_pressureColor = false;
    int m_pressureMinSize = 0;
    int m_pressureDither = 0;
    core::EasingCurve m_pressureCurve;
    bool m_tiltAngle = false;
    struct PenState {
        bool active = false;
        double pressure = 1;
        int tiltAngle = 0; // the lean as an angle (core::tiltAngle), kept while the pen stands upright
    } m_pen;
    // The brush angle the preview and the brush use: with tilt on and a pen in use, turned the way it leans.
    int effectiveAngle() const;
    int m_stampSize = 0; // the size of the stamp being made (pen pressure); 0: m_brushSize
    int stampSize() const { return m_stampSize > 0 ? m_stampSize : m_brushSize; }
    // The brushes the stroke's stamps use now (the per-stamp one with pen dynamics).
    const BrushSet* strokeBrushes() const
    {
        return m_stroke.dynamic ? m_stroke.stampBrushes : m_stroke.brushes ? &*m_stroke.brushes : nullptr;
    }
    // Sets up the stamp at `pressure` (pen dynamics): its size and brush.
    void prepareStamp(double pressure);
    // Stamps the stroke's brush at p in the pen's ink (opacity / color from the pressure level).
    core::Rect stampInk(core::Point p, double level);
    uint32_t ownInk(int x, int y, double level, bool dithered, float threshold);
    mutable core::Image m_turned;
    mutable std::array<qint64, 5> m_turnedKey{-1, -1, -1, -1, -1};
    bool m_shading = false;
    QPoint m_shadeRange{-1, -1};
    bool m_symmetryH = false;
    bool m_symmetryV = false;
    bool m_symmetryRising = false;
    int m_symmetryRadial = 0;
    bool m_symmetryRadialMirror = false;
    bool m_symmetryRadialTurn = false;
    bool m_symmetryFalling = false;
    core::Point m_symmetryCenter2; // twice the crossing point, in pixels
    core::Point m_symmetrySize;    // the canvas size the center was set for
    core::BrushShape coreBrush() const;
    // `p` (document coordinates) and its mirror images under the symmetry
    // settings, for a brush of `size` (1 for single pixels): 1, 2 or 4 points.
    std::vector<core::Point> mirrored(core::Point p, int size) const;
    // The same for a w x h brush, each copy with the core::Mirror element that
    // maps it and its share of a radial turn (identity first).
    std::vector<Copy> mirrored(core::Point p, int w, int h) const;
    // The core::Mirror elements the symmetry settings make together (identity first).
    std::vector<int> symmetryElements() const;
    core::Point fittedForDiagonals(core::Point c2, bool changeX, int direction) const;
    void fitSymmetryCenter(); // after a diagonal axis was turned on
    // `shape` and its copies under every symmetry setting (axes, radial), cut to the canvas.
    core::Mask symmetricSelection(const core::Mask& shape) const;
    // `shape` copied to where each symmetry copy of its pixels lands (radial copies turned).
    core::Mask symmetricMask(const core::Mask& shape) const;
    // The custom brush (or the built-in one of m_brushSize, both turned by the angle) and its variants;
    // in the document's format or in RGBA (for previews).
    BrushSet customBrushes(bool documentFormat, bool custom) const;
    // The angle changes the brush (not for a round brush, and not for tiny ones, where it looks the same).
    bool angleApplies() const;
    // Radial copies turn the square / custom brush with them.
    bool radialTurns() const;
    // The brush stamps as an image, not the built-in shape.
    bool brushIsImage() const { return angleApplies() || radialTurns(); }
    // The brush's pixels turned by the angle (RGBA; the built-in one in white), kept until something it depends on changes.
    const core::Image& turnedBrush(bool custom) const;
    // The brush's pixels (RGBA; the built-in one of `size` in white) turned by `angle` degrees.
    core::Image brushPixels(bool custom, int size, int angle) const;
    bool useCustomBrush(const core::Image& rgba); // trims, checks the size, selects it
    void pasteFloating(std::optional<QPoint> center); // paste() / pasteAt()
    void setDragSize(int w, int h); // the stroke's dragInfo
    bool pasteNewLayer(std::optional<QPoint> center); // pasteAsNewLayer() / ...At()
    bool customBrushInUse() const;
    // Stamps the brush at p (image coordinates of `img`, at `origin` in the
    // document) and its mirrors: the built-in brush of m_brushSize, or
    // `custom` (in its own colors with `ownColors`, else its shape in `value`).
    // With `skipFirst`, not the brush itself (the hover preview's copies).
    core::Rect stampMirrored(core::Image& img, core::Point origin, core::Point p, uint32_t value,
                             const BrushSet* custom = nullptr, bool ownColors = false, bool skipFirst = false) const;
    // The same in the stroke's shading ink.
    core::Rect shadeMirrored(core::Point p) const;
    core::Rect softenMirrored(core::Point p) const; // blur / jumble brush
    // stampMirrored with the stroke's brush and value.
    // With a `value`, its shape in that value (the dither pass's coverage).
    core::Rect stampStroke(core::Image& img, core::Point p, std::optional<uint32_t> value = {}) const
    {
        return stampMirrored(img, m_stroke.origin, p, value.value_or(m_stroke.value), strokeBrushes(),
                             m_stroke.brushOwnColors && !value);
    }
    core::Image m_sheet; // the tileset image being imported (RGBA)
    QString m_sheetName;
    core::Image m_customBrush; // RGBA; null = none. The source below, flipped and scaled.
    core::Image m_customSource;
    int m_customScale = 0;
    bool m_customFlipH = false, m_customFlipV = false;
    bool rebuildCustomBrush(int scale); // false (nothing changed) if the result is too big or empty
    int m_customBrushId = 0;   // counts new custom brushes, for brushKey()
    bool m_brushColors = true;
    bool m_dither = false;
    int m_ditherPattern = core::DitherPattern::Checker;
    int m_ditherDensity = 8;
    bool m_ditherOtherColor = false;
    bool m_pixelPerfect = true;
    bool m_fillShapes = false;
    bool m_fillContiguous = true;
    int m_fillTolerance = 0;
    bool m_wandContiguous = true;
    bool m_sampleMerged = false;
    int m_tileMode = 0;
    int m_currentSlice = -1;
    bool m_editTiles = true;
    uint32_t m_tileFlips = 0; // kTileFlip* bits
    // The stamp flipped / turned by the flip bits that changed (kTileFlip*): its cells move
    // and their flags follow, so it looks as a single tile would with those buttons.
    void flipStamp(uint32_t bits);
    // Whether the selection (when there is one) touches the cell at `cell` of a map placed at `origin`.
    bool cellTouchesSelection(core::Point cell, core::Point origin, const core::Tileset& set) const;
    // Moving selected cells (tile mode): the cells the selection touches, with their values.
    using CellList = std::vector<std::pair<core::Point, uint32_t>>;
    CellList selectedCells(const core::Image& map, core::Point origin) const;
    // `offset` (cells) limited so every cell stays in the map.
    static core::Point clampCellOffset(const CellList& cells, core::Point offset, const core::Image& map);
    // Clears the cells (unless `copy`) and puts their tiles `offset` away; empty cells of the
    // block leave what is there. Returns the cells changed.
    static core::Rect placeCells(core::Image& map, const CellList& cells, core::Point offset, bool copy);
    void beginMoveCells(QPoint pixel, Qt::KeyboardModifiers modifiers);
    void updateMoveCells();
    // The arrows: the selected cells go `dx`, `dy` cells, one undo step.
    bool nudgeCells(int dx, int dy);
    std::vector<uint32_t> m_stamp; // m_stampW x m_stampH cell values; empty: none
    int m_stampW = 0;
    int m_stampH = 0;
    QRect m_tilePickRect;
    void tileFill(core::Point cell);
    void drawTileShape();
    void finishTilePick();
    int m_tileEditMode = 1;
    // Pixel mode on a tilemap: the map as pixels for the stroke (the cel is
    // made / grown first; the tileset gets fresh tile images, copy on write).
    std::shared_ptr<core::Image> beginTilePixels(core::Point& origin, bool& created);
    void retile(core::Rect area);   // writes the stroke's cells in `area` back into the tileset
    void finishTileStroke();         // one undo step for the tileset and the map
    // The current layer's tilemap cel, grown to cover the canvas (made if
    // there's none); its origin in `origin`.
    std::shared_ptr<core::Image> editableTilemap(core::Point& origin, bool* created);
    void stampTile(core::Point pixel); // the tile stroke at a canvas pixel
    void pickTile(core::Point pixel);
    int m_currentTile = 1;
    int m_tilesetRevision = 0;
    const core::Tileset* currentTileset() const;
    void bumpTileset(); // tilesetRevision + 1, and the signal
    bool m_sliceKeys = false;
    // The key an edit at the current frame changes (made there first with sliceKeys).
    core::SliceKey* editableKey(core::Slice& slice) const;
    void editSlices(const std::function<void(std::vector<core::Slice>&)>& change, const char* label,
                    const QString& mergeKey = {});
    int m_transformPivot = 4;
    // Where the box would be with `size` and `angle` so the pivot stays where
    // it is now.
    core::Rect boxKeepingPivot(int width, int height, double angle) const;
    int m_textFont = 1;
    int m_textLetterSpacing = 0;
    int m_textLineSpacing = 1;
    int m_textAlign = 0;
    std::vector<std::shared_ptr<core::Image>> tilemapImages(size_t tilesetId) const; // each once
    std::vector<bool> usedTiles() const;
    bool applyTileEdit(const core::TileEdit& edit, const char* label);
    int m_gradientShape = 0;
    bool m_gradientRamp = false;
    QVariantList m_gradientLine;
    // The colors a gradient passes through: the palette's shade range as a ramp, or primary to secondary.
    struct GradientStop {
        double position = 0;
        QColor color;
    };
    std::vector<GradientStop> gradientStops() const;
    int m_gradientRepeat = 0;
    bool m_gradientCustom = false;
    std::vector<GradientStop> m_gradientCustomStops;
    // The gradient drawn last, still open to change: where it ran (document pixels) and the
    // history it sits at the end of (any other edit closes it).
    struct GradientEdit {
        bool valid = false;
        core::Point a, b;
        size_t undoPosition = 0;
        const core::Layer* layer = nullptr;
        int frame = 0;
        bool redrawing = false;
    };
    GradientEdit m_gradientEdit;
    void closeGradient();
    void redrawGradient();
    void checkGradient();
    void setGradientLine(core::Point from, core::Point to);
    int m_gradientDither = 0;
    bool m_snapToGrid = false;
    core::Rect snapCells(core::Rect r, core::Point origin = {}) const;
    // In tile mode on a tilemap layer, `r` (document pixels) grown to whole cells of its tileset,
    // whose grid starts at the cel's top left; otherwise as given.
    core::Rect snapToTileCells(core::Rect r) const;
    core::Point snapVertex(core::Point p, core::Point origin = {}) const;
    core::Point snapMoveBy(core::Point at, core::Point offset) const;
    int m_textCursor = 0;
    // The text just placed, so a click on it can open it again (see
    // reopenLastText): what it said, where, and the undo step it made.
    struct PlacedText {
        bool valid = false;
        QString text;
        core::Point anchor;
        core::Rect box;
        size_t undoIndex = 0;
        const core::Document* doc = nullptr;
        int frame = 0;
        const core::Layer* layer = nullptr;
    } m_placedText;
    bool reopenLastText(core::Point pixel);
    struct TextState {
        bool active = false;
        core::Point anchor; // top-left, until the float is moved
        QString text;
    } m_text;
    bool m_retypingText = false; // renderTextFloat replacing the float
    void renderTextFloat();
    core::Point m_tileShift; // from the copy a stroke started on to the sprite
    int m_wandTolerance = 0;
    core::Mask m_recordedSelection; // the selection as the undo history has it

    // Pixels not yet in the document (a paste, or lifted for a transform).
    struct Float {
        bool active = false;
        core::Image source; // document format, as pasted / lifted
        core::Image sourceMask; // Indexed8, 1 = selected, same size as source; null = all of it
        core::Mask mask;        // the selected shape, rendered like the pixels (document coordinates)
        core::Rect box;     // what the source is scaled into
        double angle = 0;   // then rotated by this, around the box center
        // The pivot's exact place while only the angle changes (rotating by
        // the handle), so the box's whole-pixel rounding doesn't drift it.
        std::optional<std::pair<double, double>> pivot;
        core::Image rendered; // document format
        core::Image rgba;     // the rendered pixels, for the overlay
        core::Point position; // of rendered
        std::string label;
        bool keepSelection = true; // select the result once it lands
        // Transform only: where the pixels came from, to land or put back.
        bool lifted = false;
        // Cmd+T on several cels: where each came from, and each one's pixels in the shared box (below).
        struct Part {
            core::Layer* layer = nullptr;
            core::CelRef cel;
            std::shared_ptr<core::Image> image;
            core::Image before; // the whole cel image before it was lifted
            core::Point origin; // the cel's place
            core::Rect area;    // lifted from this part of the image
            core::Image source; // the size of `unionBox`, with its pixels where they were
        };
        struct Multi {
            std::vector<Part> parts;
            std::vector<LiveStart> live;
            core::Rect unionBox; // all the parts together, in document coordinates
        };
        std::shared_ptr<Multi> multi;
        std::shared_ptr<core::Image> liftImage;
        core::Image liftBefore;
        core::Point liftOrigin;
        core::Rect liftArea; // image coordinates
        core::Rect rect() const { return {position.x, position.y, rendered.width(), rendered.height()}; }
    };
    Float m_float;
    void landMulti(const Float& f, bool place);
    struct Effect {
        bool active = false;
        std::shared_ptr<core::Image> image;
        core::Image before;
        core::Point origin;
        QVariantMap options; // of the last preview
        // The other cels of the current frame in the scope, previewed too
        // (the rest of the scope only changes on apply).
        struct Target {
            std::shared_ptr<core::Image> image;
            core::Image before;
            core::Point origin;
            bool alphaLock = false;
        };
        std::vector<Target> targets;
        QString scope = QStringLiteral("cel");
    } m_effect;
    bool m_modePreview = false;
    // The palette of the Reduce Colors effect (made from the sprite once per count).
    mutable core::Palette m_quantizePalette;
    mutable int m_quantizeColors = 0;
    bool m_palettePreviewing = false; // applyPalette shows its result instead of applying it
    // The sprite's own palette for a preview, kept while the dialog is open
    // (it doesn't depend on the dither): the colors asked for and the result.
    int m_previewPaletteColors = 0;
    bool m_previewPaletteAlpha = false;
    core::Palette m_previewPalette;
    std::unique_ptr<core::SetColorModeCommand> modeCommand(const QString& mode, const QString& palette,
                                                           const QString& dither, int colors,
                                                           const QVariantList& given, int onlyFrame,
                                                           bool cachePalette, bool keepAlpha,
                                                           const QVariantMap& ditherOptions);
    struct EffectCel {
        std::shared_ptr<core::Image> image;
        core::Point origin;
        bool alphaLock = false;
    };
    // The cels `scope` covers besides the current cel's image `except`, each
    // image once; with onlyCurrentFrame, just those on the current frame.
    std::vector<EffectCel> effectCels(const QString& scope, const core::Image* except, bool onlyCurrentFrame) const;
    core::Image effectResult(const QVariantMap& options, const core::Image& src, core::Point origin,
                             bool alphaLock) const;
    QImage m_copiedImage; // last pixels we copied, to recognize them on paste
    // Export preview: images (sheet, GIF) or the document frames to render.
    struct ExportPreview {
        std::vector<core::Image> images;
        std::vector<int> frames;
    } m_exportPreview;
    core::Point m_copiedOrigin;
    void startFloat(const core::Image& source, core::Rect box, const char* label);
    bool clipboardRgba(core::Image& out) const; // the clipboard's image, at most maxImageSize x maxImageSize
    void renderFloat();
    // `useSelection` false: the whole cel even with a selection (import).
    bool liftForTransform(const char* label, bool useSelection = true);
    void landFloat(bool place);

    struct TimelineClip {
        core::ColorMode mode = core::ColorMode::Rgba; // cels only paste into the same mode
        bool frames = false;
        std::vector<int> durations;                // frames only
        std::vector<std::vector<core::CelRef>> rows; // frames: per frame, cels by cel layer; cels: [row top-down][frame]
    };
    TimelineClip m_timelineClip;
    RangeType m_rangeType = NoRange;
    bool m_layerBlock = false; // the cel range was taken in the layer column: Delete removes those layers
    core::Rect m_range; // x/w frames, y/h layer indices
    void setRange(RangeType type, core::Rect range);
    // Frames and layer indices an operation applies to (range or current).
    std::pair<int, int> frameSpan() const;
    void removeFrames(int first, int last); // frames [first, last]; at least one frame stays
    std::pair<int, int> layerSpan() const;
    void setTags(std::vector<core::Tag> tags, const char* label);
    // Applies a new palette; `map` (old index -> new) remaps indexed pixels.
    void resetCanvas();
    void pushCommand(std::unique_ptr<core::Command> command);
    std::unique_ptr<core::Command> withSelection(std::unique_ptr<core::Command> command);
    void recordSelection(const char* label, bool merge = false);
    void deleteArea(const core::Mask& area, const char* label);
    void applyPalette(core::Palette palette, int transparentIndex, const std::vector<int>& map, const char* label,
                      bool keepRange = false);
    // The palette tools' entries: the shade range (or all) without an indexed
    // sprite's transparent index.
    std::vector<int> paletteToolIndices() const;
    // Pixels using each palette entry, over every cel.
    std::vector<uint64_t> paletteUsage() const;
    // The reduction of `indices` that drops unused and/or duplicate colors.
    core::ColorReduction planRemoval(const std::vector<int>& indices, bool unused, bool duplicates) const;
    bool applyReduction(const std::vector<int>& indices, const core::ColorReduction& reduced, const char* label);
    // Reorders the tools' entries: the k-th of them gets the color of entry
    // order[k] (an index into the same list); pixels follow in indexed sprites.
    bool reorderPalette(const std::vector<int>& order, const char* label);
    void replacePalette(core::Palette palette, const char* label);
    // The index map loading `incoming` into the current indexed sprite uses
    // (empty: every index stays), and the transparent index afterwards.
    std::vector<int> paletteSwapMap(const core::Palette& incoming, int& transparent) const;
    int m_paletteLoadMode = 0;
    std::optional<core::Point> m_lastPainted;
    core::Image m_overlay;
    bool m_onionEnabled = false;
    core::OnionSkin m_onionOptions;
    bool m_onionFront = false;
    bool m_onionLoop = false;
    core::Image m_onion;
    core::Image m_scratch; // document-sized, for rendering other frames
    core::Rect m_onionDirty;
    bool m_onionClear = true; // m_onion is fully transparent
    core::Rect m_overlayDirty;
    core::Image m_selectionImage; // canvas-sized, white where selected
    core::Rect m_selectionDirty;
    void drawSelectionImage(core::Rect area);
    void selectionUpdated(core::Rect oldBounds);
    core::Rect m_overlayDrawn;
    QPoint m_previewHover;
    bool m_previewActive = false;
    bool m_hoverInside = false; // the pointer is over the canvas
    bool m_lassoPolygon = false;
    // Polygon lasso being placed: its corners (document coordinates) and the
    // mode its first click picked (those keys don't also snap later corners).
    struct Polygon {
        bool active = false;
        std::vector<core::Point> corners;
        Stroke::SelectMode mode = Stroke::Replace;
        Qt::KeyboardModifiers modeModifiers;
    };
    Polygon m_polygon;
    void polygonClick(QPoint pixel, Qt::KeyboardModifiers modifiers);

    QColor m_primary{0xff, 0xf1, 0xe8};
    QColor m_secondary{0x1d, 0x2b, 0x53};
    std::vector<core::Layer*> m_layerList; // rows of the layer panel
    core::Layer* m_current = nullptr;       // current layer, always in the document
    int m_currentRow = 0;                   // its row in m_layerList
    int m_currentFrame = 0;
    // Mode last announced to QML, to emit colorModeChanged only on changes.
    core::ColorMode m_shownColorMode = core::ColorMode::Rgba;
    int m_layerCounter = 1;
    bool m_playing = false;
    QTimer m_playTimer;
    QString m_mergeKey;          // last continuous edit, for merging undo steps
    QElapsedTimer m_mergeTimer;
    std::optional<core::PlaybackCursor> m_playback;
    std::optional<core::PlaybackCursor> m_previewPlayback; // the preview panel's, see startPreviewPlayback
    int m_playingTag = -1;
    Stroke m_stroke;

    QString m_filePath;          // empty until saved or opened
    QString m_untitledName;      // shown while m_filePath is empty
    bool m_modified = false;

    // The other open documents. The current one lives in the members above;
    // its slot is empty until it's parked (switching away).
    // A tab made by editTileInTab: where its pixels go back to.
    struct TileLink {
        bool active = false;
        QString sourceId;       // the source document's recovery id (stable across reordering)
        size_t tilesetId = 0;
        int tile = 0;
        int outerMode = 0;      // the tiled view of the other tabs, put back when leaving this one
        int viewMode = 3;       // the tiled view of this tab
    };
    TileLink m_tileLink;
    void syncTileLink();
    struct DocumentSlot {
        TileLink tileLink;
        std::unique_ptr<core::Document> doc;
        core::UndoStack undo;
        core::Mask recordedSelection;
        QString filePath;
        QString untitledName;
        core::Layer* current = nullptr;
        int currentRow = 0;
        int currentFrame = 0;
        int layerCounter = 1;
        RangeType rangeType = NoRange;
        bool layerBlock = false;
        core::Rect range;
        std::optional<core::Point> lastPainted;
        core::Point symmetryCenter2;
        core::Point symmetrySize;
        QString recoveryId;
        bool autosavePending = false;
    };
    std::vector<DocumentSlot> m_docs;
    int m_currentDoc = 0;
    // The startup sprite, untouched so far: the next new / opened one replaces it.
    bool m_placeholder = false;
    void park(DocumentSlot& slot);
    void unpark(DocumentSlot& slot);
    void documentSwitched();
    QString slotName(const DocumentSlot& slot) const;
    QString nextUntitledName() const;

    RecoveryStore& recovery();
    std::unique_ptr<RecoveryStore> m_recovery; // made on first use
    QString m_recoveryRoot;
    bool m_keepRecovery = false;
    std::vector<RecoveryStore::Entry> m_recoverable;
    QTimer m_autosaveTimer;
    static constexpr int kAutosaveDelay = 15;
    static constexpr int kUndoMemory = 256; // MB
    int m_undoMemory = kUndoMemory;
    static constexpr QSize kNewGridSize{16, 16};
    int m_autosaveDelay = kAutosaveDelay;
    QSize m_newGridSize = kNewGridSize;
    QString m_recoveryId;         // the current document's recovery file
    bool m_autosavePending = false; // changed since its last autosave
};

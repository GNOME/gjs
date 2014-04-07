const JSUnit = imports.jsUnit;
const Cairo = imports.cairo;
const Everything = imports.gi.Regress;

function _ts(obj) {
    return obj.toString().slice(8, -1);
}

function _createSurface() {
    return new Cairo.ImageSurface(Cairo.Format.ARGB32, 1, 1);
}

function _createContext() {
    return new Cairo.Context(_createSurface());
}

function testContext() {
    let cr = _createContext();
    JSUnit.assertTrue(cr instanceof Cairo.Context);
}

function testContextMethods() {
    let cr = _createContext();
    JSUnit.assertTrue(cr instanceof Cairo.Context);
    cr.save();
    cr.restore();

    let surface = _createSurface();
    JSUnit.assertEquals(_ts(cr.getTarget()), "CairoImageSurface");

    let pattern = Cairo.SolidPattern.createRGB(1, 2, 3);
    cr.setSource(pattern);
    JSUnit.assertEquals(_ts(cr.getSource()), "CairoSolidPattern");
    cr.setSourceSurface(surface, 0, 0);

    cr.pushGroup();
    cr.popGroup();

    cr.pushGroupWithContent(Cairo.Content.COLOR);
    cr.popGroupToSource();

    cr.setSourceRGB(1, 2, 3);
    cr.setSourceRGBA(1, 2, 3, 4);

    cr.setAntialias(Cairo.Antialias.NONE);
    JSUnit.assertEquals("antialias", cr.getAntialias(), Cairo.Antialias.NONE);

    cr.setFillRule(Cairo.FillRule.EVEN_ODD);
    JSUnit.assertEquals("fillRule", cr.getFillRule(), Cairo.FillRule.EVEN_ODD);

    cr.setLineCap(Cairo.LineCap.ROUND);
    JSUnit.assertEquals("lineCap", cr.getLineCap(), Cairo.LineCap.ROUND);

    cr.setLineJoin(Cairo.LineJoin.ROUND);
    JSUnit.assertEquals("lineJoin", cr.getLineJoin(), Cairo.LineJoin.ROUND);

    cr.setLineWidth(1138);
    JSUnit.assertEquals("lineWidth", cr.getLineWidth(), 1138);

    cr.setMiterLimit(42);
    JSUnit.assertEquals("miterLimit", cr.getMiterLimit(), 42);

    cr.setOperator(Cairo.Operator.IN);
    JSUnit.assertEquals("operator", cr.getOperator(), Cairo.Operator.IN);

    cr.setTolerance(144);
    JSUnit.assertEquals("tolerance", cr.getTolerance(), 144);

    cr.clip();
    cr.clipPreserve();
    let rv = cr.clipExtents();
    JSUnit.assertEquals("clipExtents", rv.length, 4);

    cr.fill();
    cr.fillPreserve();
    let rv = cr.fillExtents();
    JSUnit.assertEquals("fillExtents", rv.length, 4);

    cr.mask(pattern);
    cr.maskSurface(surface, 0, 0);

    cr.paint();
    cr.paintWithAlpha(1);

    cr.stroke();
    cr.strokePreserve();
    let rv = cr.strokeExtents();
    JSUnit.assertEquals("strokeExtents", rv.length, 4);

    cr.inFill(0, 0);
    cr.inStroke(0, 0);
    cr.copyPage();
    cr.showPage();

    let dc = cr.getDashCount();
    JSUnit.assertEquals("dashCount", dc, 0);

    cr.translate(10, 10);
    cr.scale(10, 10);
    cr.rotate(180);
    cr.identityMatrix();
    let rv = cr.userToDevice(0, 0);
    JSUnit.assertEquals("userToDevice", rv.length, 2);

    let rv = cr.userToDeviceDistance(0, 0);
    JSUnit.assertEquals("userToDeviceDistance", rv.length, 2);

    let rv = cr.deviceToUser(0, 0);
    JSUnit.assertEquals("deviceToUser", rv.length, 2);

    let rv = cr.deviceToUserDistance(0, 0);
    JSUnit.assertEquals("deviceToUserDistance", rv.length, 2);

    cr.showText("foobar");

    cr.moveTo(0, 0);
    cr.setDash([1, 0.5], 1);
    cr.lineTo(1, 0);
    cr.lineTo(1, 1);
    cr.lineTo(0, 1);
    cr.closePath();
    let path = cr.copyPath();
    cr.fill();
    cr.appendPath(path);
    cr.stroke();
}

function testSolidPattern() {
    let cr = _createContext();

    let p1 = Cairo.SolidPattern.createRGB(1, 2, 3);
    JSUnit.assertEquals(_ts(p1), "CairoSolidPattern");
    cr.setSource(p1);
    JSUnit.assertEquals(_ts(cr.getSource()), "CairoSolidPattern");

    let p2 = Cairo.SolidPattern.createRGBA(1, 2, 3, 4);
    JSUnit.assertEquals(_ts(p2), "CairoSolidPattern");
    cr.setSource(p2);
    JSUnit.assertEquals(_ts(cr.getSource()), "CairoSolidPattern");
}

function testSurfacePattern() {
    let cr = _createContext();
    let surface = _createSurface();
    let p1 = new Cairo.SurfacePattern(surface);
    JSUnit.assertEquals(_ts(p1), "CairoSurfacePattern");
    cr.setSource(p1);
    JSUnit.assertEquals(_ts(cr.getSource()), "CairoSurfacePattern");
}

function testLinearGradient() {
    let cr = _createContext();
    let surface = _createSurface();
    let p1 = new Cairo.LinearGradient(1, 2, 3, 4);
    JSUnit.assertEquals(_ts(p1), "CairoLinearGradient");
    cr.setSource(p1);
    JSUnit.assertEquals(_ts(cr.getSource()), "CairoLinearGradient");
}

function testRadialGradient() {
    let cr = _createContext();
    let surface = _createSurface();
    let p1 = new Cairo.RadialGradient(1, 2, 3, 4, 5, 6);
    JSUnit.assertEquals(_ts(p1), "CairoRadialGradient");
    cr.setSource(p1);
    JSUnit.assertEquals(_ts(cr.getSource()), "CairoRadialGradient");
}

function testCairoSignal() {
    let o = new Everything.TestObj();
    let called = false;
    o.connect('sig-with-foreign-struct', function(o, cr) {
        called = true;
        JSUnit.assertEquals(_ts(cr), "CairoContext");
    });
    o.emit_sig_with_foreign_struct();
    JSUnit.assertTrue(called);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

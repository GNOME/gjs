// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

imports.gi.versions.Gdk = '3.0';
imports.gi.versions.Gtk = '3.0';

const Cairo = imports.cairo;
const {Gdk, GIMarshallingTests, GLib, Gtk, Regress} = imports.gi;

function _ts(obj) {
    return obj.toString().slice(8, -1);
}

describe('Cairo', function () {
    let cr, surface;
    beforeEach(function () {
        surface = new Cairo.ImageSurface(Cairo.Format.ARGB32, 10, 10);
        cr = new Cairo.Context(surface);
    });

    describe('context', function () {
        it('has the right type', function () {
            expect(cr instanceof Cairo.Context).toBeTruthy();
        });

        it('reports its target surface', function () {
            expect(_ts(cr.getTarget())).toEqual('ImageSurface');
        });

        it('can set its source to a pattern', function () {
            let pattern = Cairo.SolidPattern.createRGB(1, 2, 3);
            cr.setSource(pattern);
            expect(_ts(cr.getSource())).toEqual('SolidPattern');
        });

        it('can set its antialias', function () {
            cr.setAntialias(Cairo.Antialias.NONE);
            expect(cr.getAntialias()).toEqual(Cairo.Antialias.NONE);
        });

        it('can set its fill rule', function () {
            cr.setFillRule(Cairo.FillRule.EVEN_ODD);
            expect(cr.getFillRule()).toEqual(Cairo.FillRule.EVEN_ODD);
        });

        it('can set its line cap', function () {
            cr.setLineCap(Cairo.LineCap.ROUND);
            expect(cr.getLineCap()).toEqual(Cairo.LineCap.ROUND);
        });

        it('can set its line join', function () {
            cr.setLineJoin(Cairo.LineJoin.ROUND);
            expect(cr.getLineJoin()).toEqual(Cairo.LineJoin.ROUND);
        });

        it('can set its line width', function () {
            cr.setLineWidth(1138);
            expect(cr.getLineWidth()).toEqual(1138);
        });

        it('can set its miter limit', function () {
            cr.setMiterLimit(42);
            expect(cr.getMiterLimit()).toEqual(42);
        });

        it('can set its operator', function () {
            cr.setOperator(Cairo.Operator.IN);
            expect(cr.getOperator()).toEqual(Cairo.Operator.IN);
        });

        it('can set its tolerance', function () {
            cr.setTolerance(144);
            expect(cr.getTolerance()).toEqual(144);
        });

        it('has a rectangle as clip extents', function () {
            expect(cr.clipExtents().length).toEqual(4);
        });

        it('has a rectangle as fill extents', function () {
            expect(cr.fillExtents().length).toEqual(4);
        });

        it('has a rectangle as stroke extents', function () {
            expect(cr.strokeExtents().length).toEqual(4);
        });

        it('has zero dashes initially', function () {
            expect(cr.getDashCount()).toEqual(0);
        });

        it('transforms user to device coordinates', function () {
            expect(cr.userToDevice(0, 0).length).toEqual(2);
        });

        it('transforms user to device distance', function () {
            expect(cr.userToDeviceDistance(0, 0).length).toEqual(2);
        });

        it('transforms device to user coordinates', function () {
            expect(cr.deviceToUser(0, 0).length).toEqual(2);
        });

        it('transforms device to user distance', function () {
            expect(cr.deviceToUserDistance(0, 0).length).toEqual(2);
        });

        it('can call various, otherwise untested, methods without crashing', function () {
            expect(() => {
                cr.save();
                cr.restore();

                cr.setSourceSurface(surface, 0, 0);

                cr.pushGroup();
                cr.popGroup();

                cr.pushGroupWithContent(Cairo.Content.COLOR);
                cr.popGroupToSource();

                cr.setSourceRGB(1, 2, 3);
                cr.setSourceRGBA(1, 2, 3, 4);

                cr.clip();
                cr.clipPreserve();

                cr.fill();
                cr.fillPreserve();

                let pattern = Cairo.SolidPattern.createRGB(1, 2, 3);
                cr.mask(pattern);
                cr.maskSurface(surface, 0, 0);

                cr.paint();
                cr.paintWithAlpha(1);

                cr.setDash([1, 0.5], 1);

                cr.stroke();
                cr.strokePreserve();

                cr.inFill(0, 0);
                cr.inStroke(0, 0);
                cr.copyPage();
                cr.showPage();

                cr.translate(10, 10);
                cr.scale(10, 10);
                cr.rotate(180);
                cr.identityMatrix();

                cr.showText('foobar');

                cr.moveTo(0, 0);
                cr.setDash([], 1);
                cr.lineTo(1, 0);
                cr.lineTo(1, 1);
                cr.lineTo(0, 1);
                cr.closePath();
                let path = cr.copyPath();
                cr.fill();
                cr.appendPath(path);
                cr.stroke();
            }).not.toThrow();
        });

        it('has methods when created from a C function', function () {
            if (GLib.getenv('ENABLE_GTK') !== 'yes') {
                pending('GTK disabled');
                return;
            }
            Gtk.init(null);
            let win = new Gtk.OffscreenWindow();
            let da = new Gtk.DrawingArea();
            win.add(da);
            da.realize();

            cr = Gdk.cairo_create(da.window);
            expect(cr.save).toBeDefined();
            expect(cr.getTarget()).toBeDefined();
        });
    });

    describe('pattern', function () {
        it('has typechecks', function () {
            expect(() => cr.setSource({})).toThrow();
            expect(() => cr.setSource(surface)).toThrow();
        });
    });

    describe('solid pattern', function () {
        it('can be created from RGB static method', function () {
            let p1 = Cairo.SolidPattern.createRGB(1, 2, 3);
            expect(_ts(p1)).toEqual('SolidPattern');
            cr.setSource(p1);
            expect(_ts(cr.getSource())).toEqual('SolidPattern');
        });

        it('can be created from RGBA static method', function () {
            let p2 = Cairo.SolidPattern.createRGBA(1, 2, 3, 4);
            expect(_ts(p2)).toEqual('SolidPattern');
            cr.setSource(p2);
            expect(_ts(cr.getSource())).toEqual('SolidPattern');
        });
    });

    describe('surface pattern', function () {
        it('can be created and added as a source', function () {
            let p1 = new Cairo.SurfacePattern(surface);
            expect(_ts(p1)).toEqual('SurfacePattern');
            cr.setSource(p1);
            expect(_ts(cr.getSource())).toEqual('SurfacePattern');
        });
    });

    describe('linear gradient', function () {
        it('can be created and added as a source', function () {
            let p1 = new Cairo.LinearGradient(1, 2, 3, 4);
            expect(_ts(p1)).toEqual('LinearGradient');
            cr.setSource(p1);
            expect(_ts(cr.getSource())).toEqual('LinearGradient');
        });
    });

    describe('radial gradient', function () {
        it('can be created and added as a source', function () {
            let p1 = new Cairo.RadialGradient(1, 2, 3, 4, 5, 6);
            expect(_ts(p1)).toEqual('RadialGradient');
            cr.setSource(p1);
            expect(_ts(cr.getSource())).toEqual('RadialGradient');
        });
    });

    describe('path', function () {
        it('has typechecks', function () {
            expect(() => cr.appendPath({})).toThrow();
            expect(() => cr.appendPath(surface)).toThrow();
        });
    });

    describe('surface', function () {
        it('has typechecks', function () {
            expect(() => new Cairo.Context({})).toThrow();
            const pattern = new Cairo.SurfacePattern(surface);
            expect(() => new Cairo.Context(pattern)).toThrow();
        });

        it('can access the device scale', function () {
            let [x, y] = surface.getDeviceScale();
            expect(x).toEqual(1);
            expect(y).toEqual(1);
            surface.setDeviceScale(1.2, 1.2);
            [x, y] = surface.getDeviceScale();
            expect(x).toEqual(1.2);
            expect(y).toEqual(1.2);
        });

        it('can access the device offset', function () {
            let [x, y] = surface.getDeviceOffset();
            expect(x).toEqual(0);
            expect(y).toEqual(0);
            surface.setDeviceOffset(50, 50);
            [x, y] = surface.getDeviceOffset();
            expect(x).toEqual(50);
            expect(y).toEqual(50);
        });
    });

    describe('GI test suite', function () {
        describe('for context', function () {
            it('can be marshalled as a return value', function () {
                const outCr = Regress.test_cairo_context_full_return();
                const outSurface = outCr.getTarget();
                expect(outSurface.getFormat()).toEqual(Cairo.Format.ARGB32);
                expect(outSurface.getWidth()).toEqual(10);
                expect(outSurface.getHeight()).toEqual(10);
            });

            it('can be marshalled as an in parameter', function () {
                expect(() => Regress.test_cairo_context_none_in(cr)).not.toThrow();
            });
        });

        describe('for surface', function () {
            ['none', 'full'].forEach(transfer => {
                it(`can be marshalled as a transfer-${transfer} return value`, function () {
                    const outSurface = Regress[`test_cairo_surface_${transfer}_return`]();
                    expect(outSurface.getFormat()).toEqual(Cairo.Format.ARGB32);
                    expect(outSurface.getWidth()).toEqual(10);
                    expect(outSurface.getHeight()).toEqual(10);
                });
            });

            it('can be marshalled as an in parameter', function () {
                expect(() => Regress.test_cairo_surface_none_in(surface)).not.toThrow();
            });

            it('can be marshalled as an out parameter', function () {
                const outSurface = Regress.test_cairo_surface_full_out();
                expect(outSurface.getFormat()).toEqual(Cairo.Format.ARGB32);
                expect(outSurface.getWidth()).toEqual(10);
                expect(outSurface.getHeight()).toEqual(10);
            });
        });

        it('can be marshalled through a signal handler', function () {
            let o = new Regress.TestObj();
            let foreignSpy = jasmine.createSpy('sig-with-foreign-struct');
            o.connect('sig-with-foreign-struct', foreignSpy);
            o.emit_sig_with_foreign_struct();
            expect(foreignSpy).toHaveBeenCalledWith(o, cr);
        });

        it('can have its type inferred as a foreign struct', function () {
            expect(() => GIMarshallingTests.gvalue_in_with_type(cr, Cairo.Context))
                .not.toThrow();
            expect(() => GIMarshallingTests.gvalue_in_with_type(surface, Cairo.Surface))
                .not.toThrow();
        });
    });
});

describe('Cairo imported via GI', function () {
    const giCairo = imports.gi.cairo;

    it('has the same functionality as imports.cairo', function () {
        const surface = new giCairo.ImageSurface(Cairo.Format.ARGB32, 1, 1);
        void new giCairo.Context(surface);
    });

    it('has boxed types from the GIR file', function () {
        void new giCairo.RectangleInt();
    });
});

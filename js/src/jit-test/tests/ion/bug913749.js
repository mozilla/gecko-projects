y = new Float32Array(11);
x = [];

Object.defineProperty(x, 18, {
    get: (function() {
        y.length;
    }),
});
this.toSource();

y = undefined;

for (var i = 0; i < 3; i++) {
    try {
	x.toString();
	assertEq(0, 1);
    } catch (e) {
	assertEq(e.message === `y is undefined, can't access property "length" of it` ||
		 e.message === "undefined has no properties", true);
    }
}

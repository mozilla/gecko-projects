// |jit-test| error:TypeError
var o = {
    __iterator__: function() {
        return {};
    }
};
for (var j in o) {}

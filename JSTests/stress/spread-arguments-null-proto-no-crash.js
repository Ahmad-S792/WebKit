class P extends Promise {
    constructor() {
        arguments.__proto__ = null;
        super(...arguments);
    }
}

for (var i = 0; i < testLoopCount; i++)
    var p = new P(() => {});

if (p.constructor !== P)
    throw new Error("Bad assertion!");

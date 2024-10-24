var list = [
    String.raw`import v from "mod"`,
    String.raw`import * as ns from "mod"`,
    String.raw`import {x} from "mod"`,
    String.raw`import {x,} from "mod"`,
    String.raw`import {} from "mod"`,
    String.raw`import {x as v} from "mod"`,
    String.raw`export {x} from "mod"`,
    String.raw`export {v as x} from "mod"`,
    String.raw`export * from "mod"`,
    String.raw`export * as b from "Cocoa"`,
    String.raw`export * as delete from "Cocoa"`,
    String.raw`import a, { b as c } from "Cocoa"`,
    String.raw`import d, { e, f, g as h } from "Cappuccino"`,
    String.raw`import { } from "Cappuccino"`,
    String.raw`import i, * as j from "Cappuccino"`,
    String.raw`import a, { } from "Cappuccino"`,
    String.raw`import a, { b, } from "Cappuccino"`,
    String.raw`import * as from from "Matcha"`,
    String.raw`import * as as from "Cocoa"`,
    String.raw`import { default as module } from "Cocoa"`,
    String.raw`export * from "Cocoa"`,
    String.raw`export { } from "Cocoa"`,
    String.raw`export { a } from "Cocoa"`,
    String.raw`export { a as b } from "Cocoa"`,
    String.raw`export { a, b } from "Cocoa"`,
    String.raw`export { default } from "Cocoa"`,
    String.raw`export { enum } from "Cocoa"`,
    String.raw`export { default as default } from "Cocoa"`,
    String.raw`export { enum as enum } from "Cocoa"`,
];

for (let entry of list) {
    checkModuleSyntax(entry + ` with { }`);
    checkModuleSyntax(entry + ` with { type: "json" }`);
    checkModuleSyntax(entry + ` with { "type": "json" }`);
    checkModuleSyntax(entry + ` with { "type": "json", }`);
}

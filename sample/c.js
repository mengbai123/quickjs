import {AAA} from "./a.js";
import {BBB} from "./b.js";

// throw new Error("######")
// let a = 0/0"
export let CCC = AAA + BBB

export function getC() {
    throw new Error("get c error!")
    return "getC"
}

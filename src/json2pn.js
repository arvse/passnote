/* Json -> PassNote Converter */

/* jshint esversion: 6 */
/* jshint node: true */
'use strict';

const fs = require('fs');

function push_string_null(result, subarray) {
    for (let i = 0; i < subarray.length; i++) {
        result.push(subarray.charAt(i));
    }
    result.push('\0');
}

function push_fields(result, leaf) {
    for (let i = 0; i < leaf.fields.length; i++) {
        result.push('f');
        result.push(i + 1 < leaf.fields.length ? '+' : '-');
            const field = leaf.fields[i];
        push_string_null( result,"" + Math.round(Number(field.modified)/1000).toString('16'));
        push_string_null(result, field.name);
        push_string_null(result, field.value);
    }
}

function push_node(result, parent, index) {
    let node;
    let has_next = false;
    if (index < 0) {
        node = parent;
    } else {
        node = parent.children[index];
        has_next = index + 1 < parent.children.length;
    }
    result.push(node.leaf === true ? 'l' : (node.children.length === 0 ? 'e' :'h'));
    result.push(has_next ? '+' : '-');
    push_string_null(result, node.name);
    if (node.leaf === true) {
        push_fields(result, node);
    } else {
        for (let i = 0; i < node.children.length; i++) {
            push_node(result, node, i);
        }
    }
}

function main() {
    console.log('Json -> PassNote Converter - ver 1.0.01');
    if (process.argv.length < 4) {
        console.log('usage json2pn input-file output-file');
        process.exit(1);
        return;
    }

    const root = JSON.parse(fs.readFileSync(process.argv[2]));
    const result = [ 'P', 'A', 'S', 'S', 'N', 'O', 'T', 'E'];
    push_node(result,root,-1);
    for (let i=0; i <8;i++) {
    result.push('\0');
    }
    fs.writeFileSync(process.argv[3], result.join(""));
}

main();


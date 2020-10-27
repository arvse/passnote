/* PassNote -> Json Converter */

/* jshint esversion: 6 */
/* jshint node: true */
'use strict';

const fs = require('fs');

function peek_byte(stack) {
    if (stack.pos >= stack.array.length) {
        throw new Error();
    }
    return Buffer.from([stack.array[stack.pos]]).toString('utf8');
}

function scan_nonzerostring(stack, len) {
    if (stack.pos + len > stack.array.length) {
        throw new Error();
    }
    const result = stack.array.slice(stack.pos, stack.pos + len);
    stack.pos += len;
    return Buffer.from(result).toString('utf8');
}

function scan_zerostring(stack) {
    let len = 0;
    while (stack.array[stack.pos + len] != 0) {
        if (stack.pos + len >= stack.array.length) {
            throw new Error();
        }
        len++;
    }
    const result = stack.array.slice(stack.pos, stack.pos + len);
    stack.pos += len+1;
    return Buffer.from(result).toString('utf8');
}

function scan_number(stack) {
    return parseInt(scan_zerostring(stack), 16);
}

function unpack_leaf_has_field(stack) {
    return peek_byte(stack) === 'f';
}

function unpack_field(stack, hasnext) {
    const array = scan_nonzerostring(stack, 2);
    const modified = scan_number(stack);
    if (array[0] !== 'f') {
        throw new Error();
    }
    hasnext.value = array[1] === '+';
    const name = scan_zerostring(stack);
    const value = scan_zerostring(stack);
    return {
        name: name,
        value: value,
        modified: modified
    };
}

function unpack_leaf(stack) {
    const name = scan_zerostring(stack);
    const leaf = {
        leaf: true,
        name: name,
        fields: []
    };
    const hasnext = {
        value: unpack_leaf_has_field(stack)
    };
    while(hasnext.value) {
        leaf.fields.push(unpack_field(stack, hasnext));
    }
    return leaf;
}

function unpack_holder(stack,empty) {
    const name = scan_zerostring(stack);
    const holder = {
        leaf: false,
        name: name,
        children: []
    };
    const hasmore = {
        value: !empty
    };
    while(hasmore.value){ 
        holder.children.push(unpack_node(stack, hasmore));
    }
    return holder;
}

function unpack_node(stack, hasnext) {
    const array = scan_nonzerostring(stack, 2);
    hasnext.value = array[1] === '+';
    switch ( array[0] )
    {
    case 'h':
        return unpack_holder ( stack, false );
    case 'e':
        return unpack_holder ( stack, true );
    case 'l':
        return unpack_leaf ( stack );
    default:
        throw new Error();
    }
}

function unpack_tree(stack) {
    const magic = scan_nonzerostring(stack, 8);
    if (magic[0] !== 'P' ||
        magic[1] !== 'A' ||
        magic[2] !== 'S' ||
        magic[3] !== 'S' ||
        magic[4] !== 'N' ||
        magic[5] !== 'O' ||
        magic[6] !== 'T' ||
        magic[7] !== 'E') {
        throw new Error();
    }
    const result = unpack_node(stack, {});
    if (stack.pos + 8 > stack.array.length ||
        stack.array[stack.pos] !== 0 || 
        stack.array[stack.pos+1] !== 0 || 
        stack.array[stack.pos+2] !== 0 || 
        stack.array[stack.pos+3] !== 0 || 
        stack.array[stack.pos+4] !== 0 || 
        stack.array[stack.pos+5] !== 0 || 
        stack.array[stack.pos+6] !== 0 || 
        stack.array[stack.pos+7] !== 0 ) {
        throw new Error();
    }
    return result;
}

function main() {
    console.log('PassNote -> Json Converter - ver 1.0.01');
    if (process.argv.length < 4) {
        console.log('usage pn2json input-file output-file');
        process.exit(1);
        return;
    }

    const stack = {
        array: fs.readFileSync(process.argv[2]),
        pos: 0
    };
    fs.writeFileSync(process.argv[3], JSON.stringify(unpack_tree(stack)));
}

main();

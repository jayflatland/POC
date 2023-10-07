pad_w = 229.2;
pad_d = 29.9+2;
pad_h = 3;
//cube([pad_w, pad_d, pad_h]);
linear_extrude(height=pad_h)
polygon(points=[
    [0,0],
    [0, pad_d],
    [16, pad_d],
    [pad_w, 18],
    [pad_w, 0]]);

module peg(x, y, d)
{
    w = 9;
    h = 11.8;
    translate([x - w/2, y - w/2, 0])
    {
        cube([w, w, h]);
    }
    translate([x, y, 0])
    cylinder(3.3, d/2, d/2);//just for checking
}
d7_8 = 29.8;
d13_16 = 27.8;
d3_4 = 25.9;
d11_16 = 23.8;
d5_8 = 21.8;
d9_16 = 19.8;
d1_2 = 17.8;
d7_16 = 16.9;
d3_8 = 16.8;
d5_16 = 16.8;

row1 = [
    d7_8,
    d13_16,
    d3_4,
    d11_16,
    d5_8,
    d9_16,
    d1_2,
    d7_16,
    d3_8,
    d5_16
];
interspace = 1.0;
x_vals = [
    for (
        i = 0, accum = 0.0;
        i < len(row1);
        accum = accum + row1[i] + interspace, i = i + 1
    )
    accum
];

for ( i = [ 0 : len(row1) - 1] )
{
    d = row1[i];
    x = interspace + x_vals[i] + d / 2;
    y = d/2 + interspace;
    //y = pad_d / 2;
    peg(x, y, d);
    echo(x + d / 2 + interspace);
}


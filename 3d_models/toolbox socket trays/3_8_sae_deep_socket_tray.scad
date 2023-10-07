//translate([-140/2, -45/2, 0])
pad_w = 140;
pad_d = 48;
pad_h = 3;
cube([pad_w, pad_d, pad_h]);

module peg(x, y, d)
{
    w = 9;
    h = 17;
    translate([x - w/2, y - w/2, 0])
    {
        cube([w, w, h]);
    }
    translate([x, y, 0])
    cylinder(4, d/2, d/2);//just for checking
}
d7_8 = 29.9;
d13_16 = 27.9;
d3_4 = 25.8;
d3_4h = 25.4;
d11_16 = 23.9;
d5_8 = 22.2;
d9_16 = 19.8;
d1_2 = 17.9;
d7_16 = 16.9;
d3_8 = 16.9;
d5_16 = 16.9;

row1 = [d7_8, d13_16, d3_4, d3_4h, d11_16];
row2 = [d5_16, d3_8, d7_16, d1_2, d9_16, d5_8];
row1_start_x = 0.0;
row2_start_x = 15.0;

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
    x = interspace + x_vals[i] + d / 2 + row1_start_x;
    echo(x, d);
    peg(x, d/2 + interspace, d);
}

x_vals2 = [
    for (
        i = 0, accum = 0.0;
        i < len(row2);
        accum = accum + row2[i] + interspace, i = i + 1
    )
    accum
];

for ( i = [ 0 : len(row2) - 1] )
{
    d = row2[i];
    x = interspace + x_vals2[i] + d / 2 + row2_start_x;
    peg(x, pad_d - d/2 - interspace, d);
}


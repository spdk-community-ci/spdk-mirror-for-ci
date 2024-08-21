import argparse
import gdb
import inspect


# get string for the gdb.Value object
def val2str(val, name, pre = '', post = ''):
    return f'{pre}{name} = {val}{post}'

# get string for the gdb.Value returned from the expression
def expr2str(expr, pre = '', post = ''):
    try:
        val = gdb.parse_and_eval(expr)
    except gdb.error:
        return None 

    return val2str(val, expr, pre, post)

# get string for the struct member
def member2str(val, member, pre = '', post = ''):
    if member not in val.type.keys():
        return None
    return val2str(val[member], member, pre, post)

# get string for the struct members
def members2str(val, members, pre = '', post = ''):
    return '\n'.join((member2str(val, m, pre, post) for m in members if m in val.type.keys()))

# print result of the expression
def printExpr(expr, pre = '', post = ''):
    s = expr2str(expr, pre, post)
    if s is not None:
        print(s)

# print result of the expression treated as a Tailq list
# 'field' is a struct member that points to the next list element
def printExprTailqList(expr, field, pre = '', post = ''):
    try:
        val = gdb.parse_and_eval(expr)
    except gdb.error:
        None
    else:
        list = TailqList(val, field)
        print(val2str(list, expr, pre, post))


# Decorator to count tabs indentation. It calls decorated function with appropriate number of tabs.
# The indent is not increased if the self._indent is False.
class Indentation:
    INDENT_COUNTER = -1 # global counter for the current number of tabs

    def __init__(self, indent = True):
        self._indent = indent

    def __call__(self, f):
        def inner(*args):
            if self._indent:
                Indentation.INDENT_COUNTER += 1

            tabs = '\t' * (Indentation.INDENT_COUNTER + 1)
            s = f(*args, tabs)

            if self._indent:
                Indentation.INDENT_COUNTER -= 1

            return s

        return inner

class TailqList:
    def __init__(self, val, field):
        self._val = val
        self._field = field

    def __iter__(self):
        curr = self._val['tqh_first']
        while curr:
            yield curr
            curr = curr[self._field]['tqe_next']

    def iterWithLast(self):
        curr = self._val['tqh_first']
        while curr:
            yield curr
            curr = curr[self._field]['tqe_next']
        yield curr

    @Indentation()
    def __str__(self, tabs):
        val = self._val
        s = f'({val.type})\n'

        # handle the first value separately because the member name is tqh_first
        # use iterWithLast to indicate end of list
        it = self.iterWithLast()
        val = next(it)
        s += val2str(val, 'tqh_first', tabs)

        for val in it:
            s += '\n'
            s += val2str(val, f'{self._field}.tqe_next', tabs)

        return s
        

class SpdkCmd(gdb.Command):
    CMD_NAME = 'spdk'

    def __init__(self):
        super(SpdkCmd, self).__init__(SpdkCmd.CMD_NAME, gdb.COMMAND_USER)

        parser = argparse.ArgumentParser(prog=SpdkCmd.CMD_NAME, description='todo',
            epilog='The command prints all spdk variables when run without options.')

        opts = []
        opts += parser.add_argument('--print_bdevs', action='store_true',
            help='information about all bdevs').option_strings
        opts += parser.add_argument('--find_bdev', '-f', nargs=1, metavar='bdev', help='find a bdev').option_strings
        opts += parser.add_argument('--print_threads', action='store_true',
            help='information about all threads').option_strings

        self._parser = parser
        self._opts = opts

    def complete(self, text, word):
        args = gdb.string_to_argv(text)
        if word is None:
            # Return COMPLETE_COMMAND to not treat '-' as a breaking character. This is not documented gdb feature.
            # See https://sourceware.org/bugzilla/show_bug.cgi?id=32084
            return gdb.COMPLETE_COMMAND
        if len(args) != 0 and (args[-1] == '--find_bdev' or args[-1] == '-f'):
            # todo return list of bdevs?
            return gdb.COMPLETE_NONE
        if word == '':
            return self._opts

        opts = [opt for opt in self._opts if opt.startswith(word)]
        return opts

    def invoke(self, args, from_tty):
        super().dont_repeat()
        try:
            inputArgs = self._parser.parse_args(args.split())
        except:
            # nothing to do, the error and usage are printed automatically by the parser
            return
        
        if inputArgs.print_bdevs:
            print('todo print bdevs')
            return
        if inputArgs.find_bdev:
            print(f'todo find bdev {inputArgs.find_bdev[0]}')
            return
        if inputArgs.print_threads:
            print('todo print threads')
            return

        self.printAll()

    def printAll(self):
        printExpr('g_bdev_mgr')
        printExprTailqList('g_raid_bdev_list', 'global_link')

class SpdkPrinter:
    PRINT_TYPE = True

    def __init__(self, val):
        self._val = val

    def type2str(self):
        if SpdkPrinter.PRINT_TYPE == False:
            SpdkPrinter.PRINT_TYPE = True
            return ''

        post = ' ' if self._val.type.is_scalar else '\n'
        return f'({self._val.type}){post}'

class PointerPrinter(SpdkPrinter):
    PRINT_DEREFERENCE = True

    def __init__(self, val):
        super(PointerPrinter, self).__init__(val)

    @Indentation(False)
    def to_string(self, tabs):
        val = self._val
        s = f'{self.type2str()}{val.format_string(raw=True)}'
        if PointerPrinter.PRINT_DEREFERENCE and val:
            SpdkPrinter.PRINT_TYPE = False
            s += '\n'
            s += f'{val.dereference()}'

        return s

# Class to be used with 'with' keyword to disable pointer dereference. Useful to break cycles when two structs have
# pointers to themselves.
class NoDereference:
    def __enter__(self):
        self._current = PointerPrinter.PRINT_DEREFERENCE
        PointerPrinter.PRINT_DEREFERENCE = False

    def __exit__(self, *args):
        PointerPrinter.PRINT_DEREFERENCE = self._current

class Uint8_tPrinter(SpdkPrinter):
    def __init__(self, val):
        super(Uint8_tPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        return f'{self.type2str()}{int(val)}'

class CharStringPrinter(SpdkPrinter):
    def __init__(self, val):
        super(CharStringPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        return f'{self.type2str()}"{val.string()}"'

class SpdkBdevPrinter(SpdkPrinter):
    def __init__(self, val):
        super(SpdkBdevPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        s = self.type2str()
        s += members2str(val, ['name', 'product_name', 'blocklen', 'phys_blocklen', 'blockcnt'], tabs)
        return s

class SpdkBdevMgrPrinter(SpdkPrinter):
    def __init__(self, val):
        super(SpdkBdevMgrPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        s = self.type2str()
        s += 'todo'
        return s

class RaidBaseBdevInfoPrinter(SpdkPrinter):
    def __init__(self, val):
        super(RaidBaseBdevInfoPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        return 'todo'

class RaidBdevPrinter(SpdkPrinter):
    def __init__(self, val):
        super(RaidBdevPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        s = self.type2str()
        s += members2str(val, ['strip_size', 'strip_size_kb', 'strip_size_shift', 'state', 'num_base_bdevs',
            'num_base_bdevs_discovered', 'level', 'bdev'], tabs) + '\n'
        return s

class DefaultPrinter(SpdkPrinter):
    def __init__(self, val):
        super(DefaultPrinter, self).__init__(val)

    @Indentation()
    def to_string(self, tabs):
        val = self._val
        return f'{self.type2str()}{val.format_string(raw=True)}'


def spdkLookupType(val):
    #print(f'look:{str(val.type)} {val.type.code}')
    if val.type.code == gdb.TYPE_CODE_PTR and str(val.type) != 'char *':
        return PointerPrinter(val)

    match str(val.type):
        case 'uint8_t':
            return Uint8_tPrinter(val)
        case 'char *':
            return CharStringPrinter(val)
        case 'struct spdk_bdev':
            return SpdkBdevPrinter(val)
        case 'struct spdk_bdev_mgr':
            return SpdkBdevMgrPrinter(val)
        case 'struct raid_base_bdev_info':
            return RaidBaseBdevInfo(val)
        case 'struct raid_bdev':
            return RaidBdevPrinter(val)
        case _:
            return DefaultPrinter(val)

#gdb.pretty_printers = gdb.pretty_printers[:1]
if inspect.isfunction(gdb.pretty_printers[-1]) and gdb.pretty_printers[-1].__name__ == 'spdkLookupType':
    gdb.pretty_printers[-1] = spdkLookupType
else:
    gdb.pretty_printers.append(spdkLookupType)

SpdkCmd()


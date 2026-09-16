

--更新上线最高等级和职业
CREATE procedure [dbo].[Gdcn_Tgy_Active_User_Get_Update]

as

declare @topuserid varchar(20)
declare @topchalevel int
declare @topchaclass varchar(20)

declare @sql nvarchar(1000)


--得到最高等级数据，对应的ranuser库需要更改
select identity(int,1,1) as autoID, a.topuserid ,c.chalevel,c.chaclass into #temp 
FROM dbo.Gdcn_Tgy_Active_User a,ranuser.dbo.userinfo as b,chainfo c where a.topuserid=b.userid and b.usernum=c.usernum group by a.topuserid,c.chalevel,c.chaclass order by c.chalevel

select max(autoid) as autoid into #temp2 from #temp group by topuserid 

select topuserid,chalevel,chaclass from #temp 
where autoid in (select autoid from #temp2) 


declare mycursor cursor
for
select topuserid,chalevel,chaclass from #temp 
where autoid in (select autoid from #temp2) 

open mycursor

fetch next from mycursor into @topuserid,@topchalevel,@topchaclass

while (@@fetch_status=0)
begin

	update Gdcn_Tgy_Active_User set topchalevel=@topchalevel,topchaclass=@topchaclass where topuserid=@topuserid
	
	
	fetch next from mycursor into  @topuserid,@topchalevel,@topchaclass

end

drop table #temp 
drop table #temp2

close mycursor
deallocate mycursor






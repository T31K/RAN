

CREATE procedure [dbo].[Gdcn_Tgy_Active_User_Get]

as

declare @ranuserLink varchar(200)
declare @ranshopLink varchar(200)
declare @sql nvarchar(1000)

--set @ranuserLink = 'OPENDATASOURCE(''SQLOLEDB'',''Data Source=172.16.4.37;User ID=ranuser;Password=1234'')'
--set @ranshopLink = 'OPENDATASOURCE(''SQLOLEDB'',''Data Source=172.16.4.37;User ID=ranshop;Password=1234'')'

set @sql = 'truncate table Gdcn_Tgy_Active_User select identity(int,1,1) as autoID, a.userid,b.topuserid,c.chalevel,c.chaclass into #temp from ranuser.dbo.userinfo as a,ranuser.dbo.Gdcn_Tgy_Present_Relation as b,chainfo c'+
' where a.userid = b.userid and a.usernum=c.usernum and c.chalevel>b.userlevel  group by  a.userid,b.topuserid,c.chalevel,c.chaclass order by c.chalevel' + 
' select max(autoid) as autoid into #temp2 from #temp group by userid' + 
' insert into Gdcn_Tgy_Active_User(userid,topuserid,chalevel,chaclass) select userid,topuserid,chalevel,chaclass from #temp where autoid in (select autoid from #temp2)' + 
' drop table #temp' + 
' drop table #temp2'

print @sql
exec sp_executesql @sql

